#include <FFT_DOG.hpp>

int main(int argc, char** argv)
{	
	int resize = 96, cellsize = 16;
	double sigma1 = 0.5, sigma2 = 1;
	std::string train_list = "", test_list = "";
	parseArguments(argc, argv,
				   resize, cellsize, train_list, test_list);

	// prepare train data
	Data data(train_list, Data::Action::TRAIN);
	cv::Mat train_data, train_label;
	data.DataPreparation(getFeature, train_data, train_label, "fft_dog", resize, cellsize, sigma1, sigma2);

	// prepare test data
	data.update(test_list, Data::Action::TEST);
	cv::Mat test_data, test_label;
	data.DataPreparation(getFeature, test_data, test_label, "fft_dog", resize, cellsize, sigma1, sigma2);


	// test svm 
	std::cout<<"[Note]: Starting model traning phase..."<<std::endl;
	cv::Ptr<cv::ml::SVM> svm = cv::ml::SVM::create();
	// training 
	if(!exists("liveness_svm.xml"))
	{
		svm->setType(cv::ml::SVM::Types::C_SVC);
	  	svm->setKernel(cv::ml::SVM::KernelTypes::RBF);
	  	svm->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER,100,1e-6));
	  	std::cout << "[Note]: Training SVM......" << std::endl;
	  	try 
	  	{
			//svm = StatModel::train<SVM>(train_feature, ROW_SAMPLE, train_label, params);//train SVM
			svm->train(train_data, cv::ml::SampleTypes::ROW_SAMPLE, train_label);
	  		std::cout << "[Note]: Training finished......" << std::endl;
	  		svm->save("liveness_svm.xml");
	  	} 
	  	catch (cv::Exception& e) 
	  	{
			std::cout << e.msg;
	  	}
	}
	else
	{
		std::cout << "[Note]: Pre-trained SVM model found in the current directory, load it from file......" << std::endl;
		svm = cv::Algorithm::load<cv::ml::SVM>("liveness_svm.xml");
	}
  	std::cout<<"[Note]: Model traning phase complete!"<<std::endl;


	std::cout<<"[Note]: Starting model testing phase..."<<std::endl;
  	std::cout<<"[Note]: Test set size: "<<test_data.rows<<" samples"<< std::endl;
  	test(test_data, test_label, svm);

	return 0;
}

void test(cv::Mat test_data, cv::Mat test_label, cv::Ptr<cv::ml::SVM> svm)
{
	CV_Assert(test_data.rows == test_label.rows && test_data.rows>0);

	std::cout << "[Note]: Start Testing......" << std::endl;
	int count = 0;
	int correct = 0;
	ProgressBar progressBar(test_data.rows, 70, '=', '-');
	for(int r = 0; r < test_data.rows; ++r)
	{
		int response = svm->predict(test_data.row(r));
		//std::cout<<"response: "<< response << ", expected: " <<  test_label.at<int>(r, 0) << std::endl;
		if (response == test_label.at<int>(r, 0)) correct++;
		count++;
		++progressBar;
		progressBar.display();
	}
	progressBar.done();
	std::cout<<"[Note]: Current model accuracy is: " << float(correct)/count*100 << "%" << std::endl;
}

// calculate the everage enegery response at each discrete frequency
void getFeature(cv::Mat& data, cv::Mat& label, const std::vector<std::string>& filelist, 
		        const int resize, const int cellsize, const double sigma1, const double sigma2)
{
	cv::Mat srcImg;
	cv::Mat resizedImg;

	int total_ticks = filelist.size();
	ProgressBar progressBar(total_ticks, 70, '=', '-');
	for(std::vector<std::string>::const_iterator it = filelist.begin(); it != filelist.end(); ++it)
	{
        //std::cout << "filename = " << *it << std::endl; 
		srcImg = cv::imread(*it, cv::IMREAD_GRAYSCALE);
		cv::resize(srcImg, resizedImg, cv::Size(resize, resize));

		cv::Mat response;
        findFrequencyReponse(resizedImg, response, sigma1, sigma2);
		// push back sample
		data.push_back(response);

		/* prepare label feature vector */
		if((*it).find("fake") != std::string::npos)  
		{
			label.push_back(0);
		}
		else if((*it).find("living") != std::string::npos)  
		{
			label.push_back(1);
		}

		++progressBar;
		progressBar.display();
	}
	progressBar.done();
}


void findFrequencyReponse(cv::Mat& resizedImg, cv::Mat& response, const double sigma1, const double sigma2)
{
    // normalize
    //cv::Scalar mean, std;
    //cv::meanStdDev(resizedImg, mean, std);
    //std::cout <<"origin: " << resizedImg << std::endl;
    //resizedImg.convertTo(resizedImg, CV_32F);
    //resizedImg = resizedImg / cv::Mat(resizedImg.rows, resizedImg.cols, CV_32F, cv::Scalar(mean[0]/200));
    //cv::normalize(resizedImg,  resizedImg, 0, 255, cv::NORM_MINMAX);
    //std::cout <<"normed: " << resizedImg << std::endl;
    cv::Mat sample_dog_fft;
    sample_dog_fft = FFTDOG(resizedImg, sigma1, sigma2);

    //std::cout<<fr_bin<<std::endl;
    float DCEnergy = cv::sum(cv::sum(resizedImg))[0]; 
    float totalEnergy = cv::sum(cv::sum(sample_dog_fft))[0]; 

    // checking dft result image
    //std::cout<<"dftimage size:"<< sample_dog_fft.rows<< "," << sample_dog_fft.cols <<std::endl;
    //cv::imshow( "dft image", sample_dog_fft );                   // Show our image inside it.
    //cv::waitKey(0); 
    
    // note: please use square image: eg. 64*64 
    CV_Assert(sample_dog_fft.rows == sample_dog_fft.cols);
    // Allocate frequencey historgram bin: contains sample_dog_fft.rows number of bins
    // representing sample_dog_fft.row number of discrete frequency
    int max_radius = sample_dog_fft.rows/2;
    int min_radius = 0;
    cv::Mat fr_bin = cv::Mat::zeros(cv::Size(max_radius + 1 - min_radius, 1), CV_32F); // add one extra bin to store all high frequency 
    cv::Mat bin_cnt = cv::Mat::zeros(cv::Size(max_radius + 1 - min_radius, 1), CV_32F);
    // define the center of the image
    std::pair<float, float> center(0.5*(sample_dog_fft.rows-1), 0.5*(sample_dog_fft.cols-1)); 
    //std::cout<<fr_bin.rows<< " , "<< fr_bin.cols<<std::endl;



    for(int r = 0; r < sample_dog_fft.rows; ++r)
    {
        for(int c = 0; c < sample_dog_fft.cols; ++c)
        {   
            int distance = sqrt( std::pow((r-center.first), 2) + std::pow((c-center.second), 2) );
            //std::cout<< "ditance:" << distance << " pairs: " << std::pow((r-center.first), 2) <<", " <<std::pow((c-center.second), 2) << std::endl;
            if(distance >= max_radius) // high frequency
            {
                bin_cnt.at<float>(max_radius-min_radius) += 1;
                if(sample_dog_fft.at<float>(r, c)>10)
                    fr_bin.at<float>(max_radius-min_radius) += sample_dog_fft.at<float>(r, c);
            } 
            else if (distance >= min_radius)  // low frequency 
            {
                bin_cnt.at<float>(distance-min_radius) += 1;
                if(sample_dog_fft.at<float>(r, c)>10)
                    fr_bin.at<float>(distance-min_radius) += sample_dog_fft.at<float>(r, c);
            }
            // else is to low to be considered
        }
    }


    //fr_bin = fr_bin / cv::Mat(fr_bin.rows, fr_bin.cols, CV_32FC1, cv::Scalar(cv::sum( fr_bin )[0]));
    //fr_bin = fr_bin / bin_cnt; 
    /*for(int b=0; b<max_radius + 1 - min_radius;b++) 
    {
        if(bin_cnt.at<float>(b)>0)
            fr_bin.at<float>(b) =  fr_bin.at<float>(b) /bin_cnt.at<float>(b); 
    }*/
    //cv::normalize(fr_bin,fr_bin, 0, 1, cv::NORM_L2); // Transform the matrix with float values into a
   
    //fr_bin = fr_bin / cv::Mat(fr_bin.rows, fr_bin.cols, CV_32FC1, totalEnergy); 
    fr_bin = fr_bin/(totalEnergy-DCEnergy); 
    cv::Mat fr_bin2  = fr_bin.colRange(1, fr_bin.cols);
    response = fr_bin2.clone();

    //std::cout<<fr_bin<<std::endl;
    //std::cout<< cv::Mat(fr_bin.rows, fr_bin.cols, CV_32FC1, cv::Scalar(cv::sum( bin_cnt )[0])) <<std::endl;
    //std::cout<<fr_bin.rows<< " , "<< fr_bin.cols<<std::endl;    
} 

// FFT on DOG Features
cv::Mat FFTDOG(cv::Mat srcImg, const double sigma1, const double sigma2)
{
	cv::Mat XF1, XF2, DXF, output;
	int size1, size2;
	/*
    // Filter Sizes
	size1 = 2 * (int)(3*sigma1) + 3;
	size2 = 2 * (int)(3*sigma2) + 3;
	// Gaussian Filter
	cv::GaussianBlur(srcImg, XF1, cv::Size(size1, size1), sigma1, sigma1, cv::BORDER_REPLICATE);
	cv::GaussianBlur(srcImg, XF2, cv::Size(size2, size2), sigma2, sigma2, cv::BORDER_REPLICATE);
	// Difference
	DXF = XF1 - XF2;
	*/

	DXF = srcImg;
	// Discrete Fourier Transform
	// pad the image for best performance
	cv::Mat PadDXF;
	int row = cv::getOptimalDFTSize( DXF.rows );
	int col = cv::getOptimalDFTSize( DXF.cols );
	cv::copyMakeBorder(DXF, PadDXF, 0, row - DXF.rows, 0, col - DXF.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));
	// create 2 channel Mat to store real and imaginary part of the dft result
	cv::Mat planes[] = {cv::Mat_<float>(PadDXF), cv::Mat::zeros(PadDXF.size(), CV_32F)};
	cv::Mat complex;
	cv::merge(planes, 2, complex);
	cv::dft(complex, complex);
	// calcualte the dft respond magnitude
	split(complex, planes);
	cv::magnitude(planes[0], planes[1], planes[0]);
	cv::Mat dftMag = planes[0];
	
	//dftMag += cv::Scalar::all(1);                    // switch to logarithmic scale
    //log(dftMag, dftMag);

	// crop the spectrum, if it has an odd number of rows or columns
    dftMag = dftMag(cv::Rect(0, 0, dftMag.cols & -2, dftMag.rows & -2));
    // rearrange the quadrants of Fourier image  so that the origin is at the image center
    int cx = dftMag.cols/2;
    int cy = dftMag.rows/2;
    cv::Mat q0(dftMag, cv::Rect(0, 0, cx, cy));   // Top-Left - Create a ROI per quadrant
    cv::Mat q1(dftMag, cv::Rect(cx, 0, cx, cy));  // Top-Right
    cv::Mat q2(dftMag, cv::Rect(0, cy, cx, cy));  // Bottom-Left
    cv::Mat q3(dftMag, cv::Rect(cx, cy, cx, cy)); // Bottom-Right
    cv::Mat tmp;                           // swap quadrants (Top-Left with Bottom-Right)
    q0.copyTo(tmp);
    q3.copyTo(q0);
    tmp.copyTo(q3);
    q1.copyTo(tmp);                    // swap quadrant (Top-Right with Bottom-Left)
    q2.copyTo(q1);
    tmp.copyTo(q2);
    
    //cv::normalize(dftMag, dftMag, 0, 1, cv::NORM_MINMAX); // Transform the matrix with float values into a

	return dftMag;
}


void parseArguments(const int argc, const char* const* argv,
					int& resize, int& cellsize, std::string& train_list, std::string& test_list)
{
	if( argc <= 2 ) {
		printHelp();
		exit( 1 );
	}
	else if( argc > 2 ) {
		// process arguments
		for( int i = 1; i < argc - 1; i++ ) {
			if( strcmp( argv[i], "-t" ) == 0 ) {
				train_list = argv[i + 1];
				i++;
			}
			else if( strcmp( argv[i], "-v" ) == 0 ){
				test_list = argv[i + 1];
				i++;

			}
			else if( strcmp( argv[i], "-r" ) == 0 ) {
				resize = atoi( argv[i + 1] );
				i++;
			}
			else if( strcmp( argv[i], "-c" ) == 0 ) {
				cellsize = atoi( argv[i + 1] );
				i++;
			}
			else {
				std::cerr << "invalid argument: \'" << argv[i] << "\'\n";
				printHelp();
				exit( 1 );
			}
		}
	}
	if (train_list == "" or test_list == ""){
		printHelp();
		exit( 1 );
	}

}

void printHelp()
{
	std::cout << "\nUsage: ./lbp [options]" << std::endl;
	std::cout << "\nOptions:" << std::endl;
	std::cout << "\t-r  <int> - Target resize size (default=96)" << std::endl;
	std::cout << "\t-c  <int> - Desired cell size for LBP extraction (default=16)" << std::endl;
	std::cout << "\t-t <string> - Path to a txt file contains a list of training data" << std::endl;
	std::cout << "\t-v <string> - Path to a txt file contains a list of testing data" << std::endl;
}
