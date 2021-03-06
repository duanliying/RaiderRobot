/*
 * humandetect.cpp
 *
 *  Created on: Aug 7, 2012
 *      Author: udvisionlab
 */

#include "body_detector.h"

//definitions of the class
BodyDetector::BodyDetector(bool flag)
{
	lbp_descriptor = new LBPDescriptor;

	bodyMask = imread("bodyMask.jpg",0);

	bool write_flag = imwrite("bodyMask_dup.jpg",bodyMask);
	if(!write_flag)
		cout <<"Body Mask not loaded" <<endl;

	//Initializing the parameters for the HOGDescriptor
	win_size = Size(48,96);
	block_size = Size(16,16);
	block_stride = Size(8,8);
	cell_size = Size(8,8);
	nbins = 9;
	deriv_aperture = 1;
	histogram_NormType = 0;
	win_sigma = -1;
	threshold_L2hys = 0.2;
	gamma_correction = true;
	nlevels = 16;
	useGPU = flag;
	svm_coeff.clear();
	locations.clear();

	if(useGPU)
	{
		//hog_gpu = new gpu::HOGDescriptor(win_size,block_size,block_stride,cell_size,nbins,win_sigma,threshold_L2hys,gamma_correction,nlevels);
		hog_gpu = new gpu::HOGDescriptor;
		//setting the svm detector
		//svm_coeff = hog_gpu->getDefaultPeopleDetector();
		//svm_coeff = hog_gpu->getPeopleDetector48x96();
		svm_coeff = cv::HOGDescriptor::getDefaultPeopleDetector();
		hog_gpu->setSVMDetector(svm_coeff);
	}
	else
	{
		hog = new HOGDescriptor(win_size,block_size,block_stride,cell_size,nbins,deriv_aperture,win_sigma,histogram_NormType,threshold_L2hys,gamma_correction,nlevels);
		svm_coeff = hog->getDefaultPeopleDetector();
		hog->setSVMDetector(svm_coeff);
	}
	//Initialiting the parameters for the detection
	hit_threshold = 0.0;
	win_stride = Size(8,8);
	padding = Size(0,0);
	scale0 = 1.05;

	num_detections = 0;
	num_unique_detections = 0;
	detected = false;
}

BodyDetector::~BodyDetector()
{
	if(useGPU)
		delete hog_gpu;
	else
		delete hog;
	svm_coeff.clear();
}

vector<PersonProfile> BodyDetector::DetectAndDrawHumanBody(Mat& frame)
{
	vector<Rect> detections;
	Mat frame_gray,frame_small;
	pyrDown(frame,frame_small);
	//frame_small = frame.clone();
	cvtColor(frame_small,frame_gray,CV_BGR2GRAY);
	Mat frame_mask(frame_gray.rows,frame_gray.cols,CV_8UC1);
	//equalizeHist( frame_gray, frame_gray );
	if(useGPU)
	{
		gpu::GpuMat g_frame;
		g_frame.upload(frame_gray);
		hog_gpu->detectMultiScale(g_frame,locations,hit_threshold,win_stride,padding,scale0,2);
		g_frame.download(frame_gray);
	}
	else
		hog->detectMultiScale(frame_gray,locations,hit_threshold,win_stride,padding,scale0,2);

	vector<bool> person_found(unique_detections.size(),false);
	//drawing the locations
	for(int k = 0 ; k < locations.size(); k++)
	{
		num_detections++;
		int detection_id;
		// Creating the region of interest
		detected = true;
		//editing the size of the rectangle

		if(locations[k].x < 0 ) locations[k].x = 0;
		if(locations[k].y < 0 ) locations[k].y = 0;
		if(locations[k].width > frame_gray.cols - locations[k].x) locations[k].width = frame_gray.cols - locations[k].x ;
		if(locations[k].height > frame_gray.rows - locations[k].y) locations[k].height = frame_gray.rows - locations[k].y ;

		//rectangle(frame_small,locations[k],Scalar(0,255,0),1.5);

		locations[k].width *= 2;
		locations[k].height *= 2;
		locations[k].x *= 2;
		locations[k].y *= 2;

		locations[k].x += cvRound(locations[k].width*0.1);
		locations[k].width = cvRound(locations[k].width*0.8);
		locations[k].y += cvRound(locations[k].height*0.07);
		locations[k].height = cvRound(locations[k].height*0.8);

//		locations[k].x += locations[k].width/6;
//		locations[k].y += locations[k].height/12;
//		locations[k].height = 3*locations[k].height/4;
//		locations[k].width = 2* locations[k].width/3;

		Mat image_roi(frame,locations[k]);
		Image_ROI = image_roi.clone(); //copy for saving the region of interest

		// draw the region of interest on actual image
		rectangle(frame,locations[k],Scalar(0,0,255),2.0);

		// Call the create unique id procedure for feature matching
		detection_id = CreateUniqueProfile(Image_ROI);
		//PersonProfile pf_new = unique_detections[detection_id-1];

		//setting the new location of the detected individual
		unique_detections[detection_id-1].location = locations[k];
		
		//update the existance of the person in the current frame
		unique_detections[detection_id-1].found = true; // for use with Robot
		
		if(person_found.size() != unique_detections.size()) //a new person detected
			person_found.push_back(true);
		else
			person_found[detection_id-1] = true; 

		DisplayID(frame,unique_detections[detection_id-1]);

		//reinitializing the image
		Size orig_size;
		Point offset;
		//image_roi.locateROI(orig_size,offset);
		//image_roi = frame_gray(Rect(0,0,orig_size.width,orig_size.height));

	}
	
	//update the found flags in each Person's Profile
	for(int k = 0 ; k < unique_detections.size(); k++)
	{
		if(!person_found[k]) //if that individual is not present in the frame
		{	
			//update as Not Found
			unique_detections[k].found = false;
		}
	}
	
	if(locations.size() == 0) 
		detected = false;
	else
	{ // Set that there are detections in the current frame
		detected = true;	
		detections = locations;
		locations.clear();
	}
	return unique_detections;
}

// Function which checks if the person is detected before and if not, create the unique profile
int BodyDetector::CreateUniqueProfile(Mat img)
{
	//computes the LBP Descriptor
	Mat lbp_image;
	PersonProfile pf_new;
	bool match_found = false;

	Mat img_resize;
	resize(Image_ROI,img_resize,Size(SCALE_WIDTH,SCALE_HEIGHT));

	//compute the textural descriptor
	vector<float> f_v = lbp_descriptor->ComputeLBPImage(img_resize,lbp_image,bodyMask);
	vector<float> measures,c_measures;
	vector<float>::iterator it_m,it_c,it_c_min;

	//compute the color histogram
	vector<float> c_h = ComputeColorHistogram(img_resize,bodyMask);

	// Compares with unique detections through feature matching
	for(int l = 0; l < num_unique_detections; l++)
	{
		PersonProfile pf = unique_detections[l];
		//textural mesure
		float measure = FeatureMatch(pf.feature_vector,f_v);
		measures.push_back(measure);

		//cout << "Measure of LBP with Individual " << l+1 << " = " << measure <<endl;

		//color measure
		measure = FeatureMatch(pf.color_hist,c_h);
		c_measures.push_back(measure);

		//cout << "Measure of Color with Individual " << l+1 << " = " << measure <<endl;
	}

	//find the one with minimum measure

	if(!unique_detections.empty())
	{
		float c_meas = 10000.0;
		it_c_min = c_measures.begin();
		//finding the index with the minimum measure
		for(it_m = measures.begin(); it_m < measures.end(); it_m++)
		{
			// if textural descriptor is less than a threshold
			if( (*it_m) < F_THRESHOLD)
			{
				//find the minimum color histogram measure
				int index = it_m - measures.begin();
				it_c = c_measures.begin() + index;
				if( (*it_c) < c_meas)
				{
					c_meas = (*it_c);
					it_c_min = it_c;
				}
			}

		}

		if( c_meas < C_THRESHOLD) // a match is found - update the feature vector and id
		{
			match_found = true;
			int index = it_c_min - c_measures.begin();
			PersonProfile pf = unique_detections[index]; // it should not be (*it_m) but the iterator for unique detections
			//update Id and feature vector
			pf_new.id = pf.id;
			UpdateFeature(pf.feature_vector,f_v);
			UpdateFeature(pf.color_hist,c_h);
			unique_detections[index] = pf;
		}
	}

	//if no matching create a new Person Profile with the descriptor and unique id
	if(!match_found)
	{
		pf_new.feature_vector = f_v;
		pf_new.color_hist = c_h;
		num_unique_detections ++;
		pf_new.id = num_unique_detections;
		unique_detections.push_back(pf_new);
	}

	return pf_new.id;
}

//compute Color Histogram
vector<float> BodyDetector::ComputeColorHistogram(Mat img,Mat mask)
{
	Mat img_float = Mat::zeros(Size(img.cols,img.rows),CV_32FC3);
	Mat img_cie;
	img.copyTo(img_float);
	vector<float> c_h;

	//convert toe CIE-LAB colorspace
	//img_float *= 1./255;
	cvtColor(img,img_cie,CV_BGR2HSV);

	//setting the parameters for computing histograms
	int l_bins = 20;
	int a_bins = 48;
	int b_bins = 48;
	int histSize[] = {a_bins, b_bins};

	float a_ranges[] = {0,256};
	float b_ranges[] = {0,256};
	const float* ranges[] = {a_ranges,b_ranges};

	MatND hist;
	int channels[] = {0,1};

	calcHist(&img_cie,1,channels,mask,hist,2,histSize,ranges,true,false);

	//finding the number of pixels
	int num_pixels = 0;
	for(int i = 0 ; i < mask.rows ; i ++)
		for(int j = 0 ; j < mask.cols; j++)
		{
			unsigned char pixel_mask = mask.at<unsigned char>(i,j);
			if(pixel_mask != 0)
			    num_pixels++;
		}
	double maxVal = 0;
	minMaxLoc(hist,0,&maxVal,0,0);
	//converting the histogram to
	for(int a = 0 ; a < a_bins ; a++)
		for(int b = 0 ; b < b_bins; b++)
		{
			float binVal = (hist.at<float>(a,b))/num_pixels;
			c_h.push_back(binVal);
		}

	//return the histogram
	return c_h;
}

void BodyDetector::DisplayID(Mat& img,PersonProfile pf)
{
	// to display id on image
	Rect region = pf.location;
	Rect id_rect;

	//draw rectangle to display identity
	id_rect.width = region.width;
	id_rect.height = 40 * (region.width - 20)/region.width;
	id_rect.x = region.x - 1;
	id_rect.y = region.y - id_rect.height - 1;

	rectangle(img,id_rect,Scalar(100,200,100),-1);

	double text_scale = (double)(region.width - 18)/region.width;
	char s[256];
	sprintf(s,"P %d",pf.id);
	string id_name = s;
	putText(img,id_name,Point(region.x-1,region.y-3),FONT_HERSHEY_SIMPLEX,text_scale,Scalar(36,23,193));

}

// for feature matching
float BodyDetector::FeatureMatch(vector<float> model,vector<float> sample)
{
	float measure = 0.0;
	// LBP feature matching
	for(int b = 0 ; b < model.size() ; b++)
	{
		float Sb = sample[b];
		float Mb = model[b];

		if( (Sb==0) & (Mb == 0))
			measure += 0;
		else
			measure += ( (Sb - Mb)*(Sb - Mb) / (Sb + Mb) );

	}

	return measure;
}

//for updating feature
void BodyDetector::UpdateFeature(vector<float>& model_fl,vector<float> query_fl)
{
	// update the model feature by the running average
	for(int k = 0 ; k < model_fl.size(); k++)
	{
		model_fl[k] = (1 - ALPHA) * model_fl[k] + ALPHA* query_fl[k];
	}
}

//clearing unique detections
void BodyDetector::ClearUniqueDetections()
{
	unique_detections.clear();	
	num_detections = 0;
	num_unique_detections = 0;
	detected = false;
}
