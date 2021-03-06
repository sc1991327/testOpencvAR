/*************
Copyright (c) 2011, Allen Choong
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************/

#include <stdio.h>
#include <iostream>
#include <vector>
#include <time.h>

#ifdef __WINDOWS__
# include <windows.h>
#endif

#include "cv.h"
#include "highgui.h"

#include "opencvar.h"
#include "acmath.h"


using namespace std;

int AC_CV_DEBUG = 0;


static int g_thresh = -1; //For storing auto threshold value
static int g_needRandom = 1; //For checking need randomise the threshold or not
static int g_isRandom = 0; //Check randomise performed or not

int cvarReadCamera(const char* filename,CvarCamera* pCam) {
	//Use default value
	if(!filename) {
		pCam->width = 320;
		pCam->height = 240;
		pCam->flags = 0;
		
		pCam->avg_error = 0;
		double mat[] = { 350,	0,	160,
						0,	350,	120,
						0,	0,	1};
		memcpy(pCam->matrix,mat,sizeof(double)*9);
		
		double distort[] = {0,0,0,0};
		memcpy(pCam->distortion,distort,sizeof(double)*4);
	}
	else {
		CvFileStorage* file = cvOpenFileStorage(filename,0,CV_STORAGE_READ);
		if(!file)
			return 0;
		
		pCam->width = cvReadIntByName(file,0,"image_width");
		pCam->height = cvReadIntByName(file,0,"image_height");
		pCam->flags = cvReadIntByName(file,0,"flags");
		
		pCam->avg_error = cvReadRealByName(file,0,"avg_reprojection_error");
		
		CvMat* camera = (CvMat*)cvRead(file,cvGetFileNodeByName(file,0,"camera_matrix"));
		//Copy camera matrix
		memcpy(pCam->matrix,camera->data.db,sizeof(double)*9);
		
		CvMat* distortion = (CvMat*)cvRead(file,cvGetFileNodeByName(file,0,"distortion_coefficients"));
		memcpy(pCam->distortion,distortion->data.db,sizeof(double)*4);
		
		cvReleaseFileStorage(&file);
	}
	
	//Create OpenGL projection
	cvarCameraProjection(pCam,pCam->projection);
	acMatrixTransposed(pCam->projection);
	
	return 1;
}

void cvarCameraScale(CvarCamera* pCam,int width,int height) {
	//This scaling algorithm refer to ARToolKit arParamChangeSize()
	
	//Get ratio
	float rt_u = (float)width/pCam->width;
	float rt_v = (float)height/pCam->height;
	
	//ARToolKit only uses one ratio. But I used two ratio, so that, the data of the matrix
	// need to be scaled separately
	//fx,fy (focal length)
	pCam->matrix[0] *= rt_u;
	pCam->matrix[1*3+1] *= rt_v;
	
	//cx,cy (principal point)
	pCam->matrix[2] *= rt_u;
	pCam->matrix[1*3+2] *= rt_v;
	
	pCam->width = width;
	pCam->height = height;
	
	//Recalculate OpenGL projection
	cvarCameraProjection(pCam,pCam->projection);
	acMatrixTransposed(pCam->projection);
}

void cvarCameraProjection(CvarCamera* pCam,double* projection,int glstyle) {
	//The projection should be 4x4 matrix
	
	//Set the near plane and far plane, based on ARToolKit
	//No more based on ARToolKit
	float nearplane = 0.1f;
	float farplane = 5000.0f;
	
	//Initialise with 0
	memset(projection,0,sizeof(double)*16);
	
	projection[0] = 2. * pCam->matrix[0] / pCam->width;
	projection[1*4+1] = 2. * pCam->matrix[1*3+1] / pCam->height;
	projection[0*4+2] = 2. * (pCam->matrix[2]/pCam->width) - 1.;
	projection[1*4+2] = 2. * (pCam->matrix[1*3+2]/pCam->height) - 1.;
	projection[2*4+2] = -(farplane + nearplane) / (farplane - nearplane);
	projection[2*4+3] = -2. * farplane * nearplane / (farplane-nearplane);
	projection[3*4+2] = -1;
	
	if(glstyle)
		acMatrixTransposed(projection);
	
}


/**
 * OpenCV dummy function. Because of cvLoad() for Haar detection might cause problem,
 * this function can be used.
 */
void cvarDummy() {
	IplImage* dummy = cvCreateImage(cvSize(1,1),IPL_DEPTH_8U,1);
	cvErode(dummy,dummy);
	cvReleaseImage(&dummy);
}

/**
 * Convert to OpenGL Matrix
 * \param modelview	16 size
 */
void cvarGlMatrix(double* modelview,CvMat* rotate3,CvMat* translate) {
	memset(modelview,0,16*sizeof(double));
	for(int j=0;j<3;j++) {
		for(int i=0;i<3;i++) {
			modelview[i*4+j] = rotate3->data.db[j*3+i];
		}
	}
	
	double qua[4];
	acMatrixToQuaternion(modelview,qua);
	qua[1] = -qua[1];
	qua[2] = -qua[2];
	
	acQuaternionToMatrix(qua,modelview);
	
	modelview[12] = translate->data.db[0];
	modelview[13] = translate->data.db[1];
	modelview[14] = -translate->data.db[2];
	modelview[15] = 1;
}

// returns sequence of squares detected on the image.
// the sequence is stored in the specified memory storage
CvSeq* cvarFindSquares( IplImage* img, CvMemStorage* storage,int threshold,int inner)
{
    CvSeq* contours;
    int i, c, l, N = 11;
    CvSize sz = cvSize( img->width & -2, img->height & -2 );
    IplImage* timg = cvCloneImage( img ); // make a copy of input image
    IplImage* gray = cvCreateImage( sz, 8, 1 ); 
    IplImage* pyr = cvCreateImage( cvSize(sz.width/2, sz.height/2), 8, 3 );
    IplImage* tgray;
    CvSeq* result;
    double s, t;
	
    // create empty sequence that will contain points -
    // 4 points per square (the square's vertices)
    CvSeq* squares = cvCreateSeq( 0, sizeof(CvSeq), sizeof(CvPoint), storage );
    
    // select the maximum ROI in the image
    // with the width and height divisible by 2
    cvSetImageROI( timg, cvRect( 0, 0, sz.width, sz.height ));
    
    // down-scale and upscale the image to filter out the noise
    cvPyrDown( timg, pyr, 7 );
    cvPyrUp( pyr, timg, 7 );
    tgray = cvCreateImage( sz, 8, 1 );
    
    // find squares in every color plane of the image
    for( c = 0; c < 1; c++ )
    {
        // extract the c-th color plane
		//Convert the image to greyscale
		cvCvtColor(timg,tgray,CV_BGR2GRAY);
        //cvSetImageCOI( timg, c+1 );
        //cvCopy( timg, tgray, 0 );
        
        // try several threshold levels
		//Modified: using only one threshold value
        for( l = 0; l < 1; l++ )
        {
			cvThreshold(tgray,gray,threshold,255,CV_THRESH_BINARY);
			
			if(AC_CV_DEBUG) {
				gray->origin=1;
				cvNamedWindow("find_d",1);
				cvShowImage("find_d",gray);
			}
            
            // find contours and store them all as a list
            cvFindContours( gray, storage, &contours, sizeof(CvContour),
                CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE, cvPoint(0,0) );
			            
            // test each contour
            while( contours )
            {
                // approximate contour with accuracy proportional
                // to the contour perimeter
                result = cvApproxPoly( contours, sizeof(CvContour), storage,
                    CV_POLY_APPROX_DP, cvContourPerimeter(contours)*0.02, 0 );
				
                // square contours should have 4 vertices after approximation
                // relatively large area (to filter out noisy contours)
                // and be convex.
                // Note: absolute value of an area is used because
                // area may be positive or negative - in accordance with the
                // contour orientation
				//Modified: only positive contour will be returned
				
				//Check contour
				int chkContour;
				switch(inner) {
				case 0:
					chkContour = cvContourArea(result,CV_WHOLE_SEQ) > 500;
					break;
				case 1:
					chkContour = cvContourArea(result,CV_WHOLE_SEQ) < -500;
					break;
				case 2:
					chkContour = fabs(cvContourArea(result,CV_WHOLE_SEQ)) > 500;
				}
				
                if( result->total == 4 &&
                    chkContour &&
                    cvCheckContourConvexity(result) )
                {
					/*s = 0;
                    
                    for( i = 0; i < 5; i++ )
                    {
                        // find minimum angle between joint
                        // edges (maximum of cosine)
                        if( i >= 2 )
                        {
                            t = fabs(acAngle(
								(AcPointi*)(CvPoint*)cvGetSeqElem( result, i ),
								(AcPointi*)(CvPoint*)cvGetSeqElem( result, i-2 ),
								(AcPointi*)(CvPoint*)cvGetSeqElem( result, i-1 )));
                            s = s > t ? s : t;
                        }
                    }//*/
                    
                    // if cosines of all angles are small
                    // (all angles are ~90 degree) then write quandrange
                    // vertices to resultant sequence 
					
                    //if( s < 0.3 )
					//Only if the square is smaller than the image
					
					//Modified: No need calculation of the angle
                    if( //s < 0.3 &&
						((CvPoint*)cvGetSeqElem(result,0))->x > 2 &&
						((CvPoint*)cvGetSeqElem(result,0))->x < img->width - 2 &&
						((CvPoint*)cvGetSeqElem(result,0))->y > 2 &&
						((CvPoint*)cvGetSeqElem(result,0))->y < img->height - 2 )
						{
						for( i = 0; i < 4; i++ )
                            cvSeqPush( squares,
								(CvPoint*)cvGetSeqElem( result, i ));
					}
                }
                
                // take the next contour
                contours = contours->h_next;
            }
        }
    }
    
    // release all the temporary images
    cvReleaseImage( &gray );
    cvReleaseImage( &pyr );
    cvReleaseImage( &tgray );
    cvReleaseImage( &timg );
	
    return squares;
}


/**
 * Initialise the points of square in 3D
 * \param mat	Must be 4*3 matrix with 64f (double)
 */
void cvarSquareInit(CvMat* mat,float ratio) {
	mat->data.db[0] = -ratio;
	mat->data.db[1] = -1;
	mat->data.db[2] = 0;
	
	mat->data.db[3] = ratio;
	mat->data.db[4] = -1;
	mat->data.db[5] = 0;
	
	mat->data.db[6] = ratio;
	mat->data.db[7] = 1;
	mat->data.db[8] = 0;
	
	mat->data.db[9] = -ratio;
	mat->data.db[10] = 1;
	mat->data.db[11] = 0;
}

void cvarReverseSquare(CvPoint2D32f sq[4]) {
	CvPoint2D32f temp;
	temp = sq[1];
	sq[1] = sq[3];
	sq[3] = temp;
}

/**
 * Find the camera based on the points
 * \param cam	The camera data, in order to get camera matrix and distortion
 * \param objPts	Matrix of object points in 3D, follow the function cvFindExtrinsicCameraParams2
 * \param imgPts	Matrix of image points in 2D, same as above
 * \param modelview	[Out] The model view of OpenGL matrix,
 */
void cvarFindCamera(CvarCamera* cam,CvMat* objPts,CvMat* imgPts,double* modelview) {
	CvMat camera = cvMat(3,3,CV_64F,cam->matrix);
	CvMat dist = cvMat(1,4,CV_64F,cam->distortion);
	
	CvMat* rotate = cvCreateMat(1,3,CV_64F);
	CvMat* rotate3 = cvCreateMat(3,3,CV_64F);
	CvMat* translate = cvCreateMat(1,3,CV_64F);
	
	cvFindExtrinsicCameraParams2(objPts,imgPts,&camera,&dist,rotate,translate);
	cvRodrigues2(rotate,rotate3);
	
	cvarGlMatrix(modelview,rotate3,translate);
	
	cvReleaseMat(&rotate);
	cvReleaseMat(&rotate3);
	cvReleaseMat(&translate);
}


/*******************
Face detect
*******************/
CvarHaar::CvarHaar() {
	storage = cvCreateMemStorage(0);
	cascade = 0;
	object = 0;
}
CvarHaar::CvarHaar(const char* filename) {
	storage = cvCreateMemStorage(0);
	object = 0;
	cascade = 0;
	Load(filename);
}
CvarHaar::~CvarHaar() {
	cvReleaseMemStorage(&storage);
	object = 0;
	cascade = 0;
}
void CvarHaar::Load(const char* filename) {
	cascade = (CvHaarClassifierCascade*)cvLoad(filename,0,0,0);
}

void CvarHaar::ClearMemory() {
	cvClearMemStorage(storage);
}

CvSeq* CvarHaar::DetectObjects(IplImage* image,
	double scale_factor,
	int min_neighbors,
	int flags,
	CvSize min_size) {

	object = cvHaarDetectObjects(image,cascade,storage,scale_factor,min_neighbors,flags,min_size);
	return object;
}

/**************
Optical flow
*************/
/**
 * From LKDemo of OpenCV
 */
CvarOpticalFlow::CvarOpticalFlow() {
	grey = prevGrey = pyramid = prevPyramid = 0;
	points[0] = points[1] = 0;
	flags = 0;
	status = 0;
	
	m_bInit = 0;
}

CvarOpticalFlow::~CvarOpticalFlow() {
	Destroy();
}
	
/**
 * Initialise with the image size and number of points
 * \param nPoint	Number of points
 */
void CvarOpticalFlow::Init(IplImage* img,int nPoint,CvPoint2D32f* pts) {		
	if(!m_bInit) {
		grey = cvCreateImage(cvGetSize(img),8,1);
		prevGrey = cvCreateImage(cvGetSize(img),8,1);
		pyramid = cvCreateImage(cvGetSize(img),8,1);
		prevPyramid = cvCreateImage(cvGetSize(img),8,1);
		
		points[0] = (CvPoint2D32f*)cvAlloc(nPoint * sizeof(CvPoint2D32f));
		points[1] = (CvPoint2D32f*)cvAlloc(nPoint * sizeof(CvPoint2D32f));
		
		status = (char*)cvAlloc(nPoint);
		m_bInit = 1;
	}
	
	for(int i=0;i<nPoint;i++)
		points[1][i] = pts[i];
	
	cvCvtColor(img,grey,CV_BGR2GRAY);
	
	CV_SWAP(prevGrey,grey,swapTemp);
	CV_SWAP(prevPyramid,pyramid,swapTemp);
	CV_SWAP(points[0],points[1],swapPoints);
}

/**
 * Destroy
 */
void CvarOpticalFlow::Destroy() {
	if(grey)
		cvReleaseImage(&grey);
	if(prevGrey)
		cvReleaseImage(&prevGrey);
	if(pyramid)
		cvReleaseImage(&pyramid);
	if(prevPyramid)
		cvReleaseImage(&prevPyramid);
	if(points[0])
		cvFree(&points[0]);
	if(points[1])
		cvFree(&points[1]);
	if(status)
		cvFree(&status);
	
	flags = 0;
	status = 0;
	
	m_bInit = 0;
}

/**
 * Update with the image and number of points
 * \param nPoint	Number of points
 * \param pts	[out] Points
 */
int CvarOpticalFlow::Update(IplImage* img,int nPoint,CvPoint2D32f* pts,int draw) {
	cvCvtColor(img,grey,CV_BGR2GRAY);
	cvCalcOpticalFlowPyrLK(prevGrey,grey,prevPyramid,pyramid,
		points[0],points[1],nPoint,cvSize(10,10),3,status,0,
		cvTermCriteria(CV_TERMCRIT_ITER|CV_TERMCRIT_EPS,20,0.03),flags);
	flags |= CV_LKFLOW_PYR_A_READY;
	
	CV_SWAP(prevGrey,grey,swapTemp);
	CV_SWAP(prevPyramid,pyramid,swapTemp);
	CV_SWAP(points[0],points[1],swapPoints);
	
	//Copy to output
	for(int i=0;i<nPoint;i++) {
		pts[i] = points[0][i];
	}
	
	//Calculate point
	int res = 0;
	for(int i=0;i<nPoint;i++) {
		if(status[i]) {
			res++;

			if(draw) {
				cvCircle(img,cvPointFrom32f(points[0][i]),3,CV_RGB(0,255,0),-1,8,0);
			}
		}
	}
	
	return res;
}

/*******************
Augmented Reality
****************/

/**
 * Load the template
 * \param tpl [out]	The struct store the template
 */
int cvarLoadTemplate(CvarTemplate* tpl,const char* filename,int type) {	
	IplImage* file = cvLoadImage(filename,CV_LOAD_IMAGE_GRAYSCALE);
	if(!file) {
		return 0;
	}
	
	//Create matrix
	CvMat *mat = cvCreateMat(2,3,CV_64F);
	cv2DRotationMatrix(cvPoint2D32f(file->width/2-1,file->height/2-1),90,1,mat);
	
	//Generate 4 orientation
	for(int i=0;i<4;i++) {
		tpl->image[i] = cvCloneImage(file); //In greyscale
	}
	
	//First one no rotation
	for(int i=1;i<4;i++) {
		cvWarpAffine(tpl->image[i-1],tpl->image[i],mat);
	}
	
	
	cvReleaseMat(&mat);
	cvReleaseImage(&file);
	
	//Type
	tpl->type = type;
	
	return 1;
}


int cvarLoadTemplateTag(CvarTemplate* tpl,const char* filename) {
	IplImage* file = cvLoadImage(filename,CV_LOAD_IMAGE_GRAYSCALE);
	if(!file) {
		return 0;
	}

	//Crop image
	cvSetImageROI(file,cvRect(1,1,8,8));
		
	//Binarise
	IplImage* fileg = cvCreateImage(cvGetSize(file),8,1);
	cvCopy(file,fileg);
	cvThreshold(fileg,fileg,100,1,CV_THRESH_BINARY);
	cvFlip(fileg,fileg);
	
	long long int bit;
	acArray2DToBit((unsigned char*)fileg->imageData,fileg->width,fileg->height,&bit);
	
	
	cvarLoadTag(tpl,bit);
	
	cvReleaseImage(&fileg);
	cvResetImageROI(file);
	
	return 1;
}

void cvarLoadTag(CvarTemplate* tpl,long long int bit) {
	tpl->type = 2;
	for(int i=0;i<4;i++) {
		tpl->code[i] = bit;
		acBitRotate(&tpl->code[i],i);
	}
}

/**
 * Free memory
 */
void cvarReleaseTemplate(CvarTemplate* tpl) {
	if(tpl->type == 2) //ARTag no template
		return;
	for(int i=0;i<4;i++) {
		cvReleaseImage(&tpl->image[i]);
	}
}

void cvarThresholdTemplate(CvarTemplate* tpl,int threshold) {
	if(tpl->type == 2)
		return;
	for(int i=0;i<4;i++) {
		cvThreshold(tpl->image[i],tpl->image[i],threshold,255,CV_THRESH_BINARY);
	}
}


/**
 * Compare the square with the four points of the optical flow
 * \param points [in]	Four points from optical flow
 */
int cvarCompareSquare(IplImage* img,CvPoint2D32f* points) {
	CvMemStorage* storage = cvCreateMemStorage();
	
	//Find the square
	CvSeqReader reader;
	CvSeq* square = cvarFindSquares(img,storage);
	
	cvStartReadSeq(square,&reader,0);
	
	int match = 0;
	
	for(int i=0;i<square->total;i+=4) {
		CvPoint pt[4], *rect = pt;
		int count =4;
		
		for(int j=0;j<4;j++)
			CV_READ_SEQ_ELEM(pt[j],reader);
		
		//Compare the points
		for(int j=0;j<4;j++) {
			for(int k=0;k<4;k++) {
				AcPointf tPoint;
				tPoint.x = pt[k].x;
				tPoint.y = pt[k].y;
				
				AcPointf tPoint2;
				tPoint2.x = points[j].x;
				tPoint2.y = points[j].y;
				
				double dist = acCalcLength(tPoint2,tPoint);
				
				if(dist<10) {
					match++;
				}
			}
		}
	}
	
	cvReleaseMemStorage(&storage);
	return match;
}


int cvarDrawSquares( IplImage* img, CvSeq* squares,CvPoint2D32f* points,int draw )
{
    CvSeqReader reader;
    IplImage* cpy = cvCloneImage( img );
    int i;
	
	//result
	int res = 0;
    
    // initialize reader of the sequence
    cvStartReadSeq( squares, &reader, 0 );
    
    // read 4 sequence elements at a time (all vertices of a square)
    for( i = 0; i < squares->total; i += 4 )
    {
        CvPoint pt[4], *rect = pt;
        int count = 4;
        
        // read 4 vertices
        CV_READ_SEQ_ELEM( pt[0], reader );
        CV_READ_SEQ_ELEM( pt[1], reader );
        CV_READ_SEQ_ELEM( pt[2], reader );
        CV_READ_SEQ_ELEM( pt[3], reader );
        
        // draw the square as a closed polyline 
        cvPolyLine( cpy, &rect, &count, 1, 1, CV_RGB(0,255,0), 1, CV_AA, 0 );
		
		
		//Copy the points
		for(int j=0;j<4;j++) {
			points[j].x = pt[j].x;
			points[j].y = pt[j].y;
		}
		
		res++;
    }
    
	if(draw) {
		cvCopy(cpy,img);
	}
    cvReleaseImage( &cpy );
	
	//Clear memory
	//cvClearMemStorage(g_storage);
	
	return res;
}

int cvarGetSquare( IplImage* img, CvSeq* squares,CvPoint2D32f* points)
{
    CvSeqReader reader;
    IplImage* cpy = cvCloneImage( img );
    int i;
	
	//result
	int res = 0;
    
    // initialize reader of the sequence
    cvStartReadSeq( squares, &reader, 0 );
    
    // read 4 sequence elements at a time (all vertices of a square)
    for( i = 0; i < squares->total; i += 4 )
    {
        CvPoint pt[4], *rect = pt;
        int count = 4;
        
        // read 4 vertices
        CV_READ_SEQ_ELEM( pt[0], reader );
        CV_READ_SEQ_ELEM( pt[1], reader );
        CV_READ_SEQ_ELEM( pt[2], reader );
        CV_READ_SEQ_ELEM( pt[3], reader );
        
        // draw the square as a closed polyline 
        cvPolyLine( cpy, &rect, &count, 1, 1, CV_RGB(0,255,0), 1, CV_AA, 0 );
		
		
		//Copy the points
		for(int j=0;j<4;j++) {
			points[j].x = pt[j].x;
			points[j].y = pt[j].y;
		}
		
		res++;
    }
    
	if(AC_CV_DEBUG) {
		cvNamedWindow("square_d",1);
		cvShowImage("square_d",cpy);
	}
    cvReleaseImage( &cpy );
		
	return res;
}

/**
 * Create square value, no need release
 * \param src	The array
 * \param ccw	Counter clockwise of the point
 */
void cvarSquare(CvPoint2D32f* src,int width,int height,int ccw) {
	//Reverse order is correct way
	if(ccw) {
		src[0].x = 0;	src[0].y = 0;
		src[1].x = 0;	src[1].y = height-1;
		src[2].x = width-1;	src[2].y = height-1;
		src[3].x = width-1;	src[3].y = 0;
	}
	else {
		src[0].x = 0;	src[0].y = 0;
		src[1].x = width-1;	src[1].y = 0;	
		src[2].x = width-1;	src[2].y = height-1;
		src[3].x = 0;	src[3].y = height-1;
	}
}


/**
 * Rotate square
 * \param rot	The rotation. 1 - 0 degree, 2 - 90 degree ccw, 3 - 180 degree, 4 - 270 degree ccw
 */
void cvarRotSquare(CvPoint2D32f* src,int rot) {
	CvPoint2D32f temp[4];
	for(int i=0;i<4;i++) {
		temp[i] = src[i];
	}
	
	//The fomula come from:
	/* switch(rot) {
	case 1:
		src[0] = temp[0];
		src[1] = temp[1];
		src[2] = temp[2];
		src[3] = temp[3];
	case 2:
		src[1] = temp[0];
		src[2] = temp[1];
		src[3] = temp[2];
		src[0] = temp[3];
		break;
	case 3:
		src[2] = temp[0];
		src[3] = temp[1];
		src[0] = temp[2];
		src[1] = temp[3];
		break;
	case 4:
		src[3] = temp[0];
		src[0] = temp[1];
		src[1] = temp[2];
		src[2] = temp[3];
		break;
	default:
		break;
	}*/
	for(int i=0;i<4;i++) {
		src[(rot-1+i)%4] = temp[i];
	}
}

/**
 * Invert perspective view of the image to the 2D view
 * \param input	Input image, the whole image
 * \param output	Output image.
 * \param src	Source 4 points, correspond to input
 * \param dst	Destination 4 points, correspond to output
 */
void cvarInvertPerspective(IplImage* input,IplImage* output,CvPoint2D32f* src,CvPoint2D32f* dst) {
	CvMat* mapMatrix = cvCreateMat(3,3,CV_32FC1);
	cvGetPerspectiveTransform(src,dst,mapMatrix);
	cvWarpPerspective(input,output,mapMatrix);
	cvReleaseMat(&mapMatrix);
}


int cvarGetOrientation(IplImage* input,CvarTemplate tpl,double* match,double thres) {
	//Convert input to gray
	IplImage* gray = cvCreateImage(cvSize(input->width,input->height),IPL_DEPTH_8U,1);
	//gray->origin = 1;
	if(input->nChannels != 1) {
		cvCvtColor(input,gray,CV_BGR2GRAY);
		cvFlip(gray,gray); //Because of some circumstance);
	}
	else
		cvCopy(input,gray);
	
	double prevVal = -1;
	int res = -1;
	for(int i=0;i<4;i++) {

		//Using template matching
		IplImage* result = cvCreateImage(cvSize(1,1),IPL_DEPTH_32F,1);
		result->origin=1;
		cvMatchTemplate(gray,tpl.image[i],result,CV_TM_CCORR_NORMED);
		double minval,maxval;
		cvMinMaxLoc(result,&minval,&maxval);
		cvReleaseImage(&result);
		
		if(AC_CV_DEBUG)
			printf("%d, maxval: %f\tminval: %f\n",i+1,maxval,minval);

		if((maxval > thres) && (maxval > prevVal)) {
			prevVal = maxval;
			
			//To output
			if(match)
				*match = maxval;
			res = i;
		}
		

	}
	cvReleaseImage(&gray);

	return res + 1;
}

/**
 * From the points of the square to OpenGL model view matrix
 * \param points	Points of the square
 * \param cam	Camera
 * \param modelview [out]	Model view matrix
 */
void cvarSquareToMatrix(CvPoint2D32f* points,CvarCamera* cam,double* modelview,float ratio) {
	//Calculate matrix
	CvMat* imgPoint = cvCreateMat(4,2,CV_64F);
	CvMat* objPoint = cvCreateMat(4,3,CV_64F);
	cvarSquareInit(objPoint,ratio);
	
	for(int i=0;i<4;i++) {
		imgPoint->data.db[i*2] = points[i].x;
		imgPoint->data.db[i*2+1] = points[i].y;
	}
	
	cvarFindCamera(cam,objPoint,imgPoint,modelview);
	
	cvReleaseMat(&imgPoint);
	cvReleaseMat(&objPoint);
}


/**
 * Calculate the model view matrix from square of the image.
 * \param img	Image
 * \param points [out]	4 points of the square in array
 * \return 1 if found, else 0
 */
int cvarArRegistration(IplImage* img,CvPoint2D32f* points,CvarTemplate tpl,int thresh,double matchThresh) {
	CvMemStorage* storage = cvCreateMemStorage();
	CvMemStorage* patStorage = cvCreateMemStorage();
	
	//Greyscaling
	IplImage* grey = cvCreateImage(cvSize(img->width,img->height),8,1);
	cvCvtColor(img,grey,CV_BGR2GRAY);
	//cvThreshold(grey,grey,100,255,CV_THRESH_BINARY);
	cvCvtColor(grey,img,CV_GRAY2BGR);
	cvReleaseImage(&grey);
	
	int res = 0;
	
	//Find square
	int square = 0;
	//CvPoint2D32f points[4] = {0};
	square = cvarGetSquare(img,cvarFindSquares(img,storage,thresh),points);
	if(square) {
		//Get image within the square
		int pattern = 0;
		CvPoint2D32f patPoint[4] = {0};
		CvPoint2D32f patPointSrc[4];
		cvarSquare(patPointSrc,tpl.image[0]->width,tpl.image[0]->height,1); //Need to use ccw, don't know why
		
		pattern = cvarGetSquare(img,cvarFindSquares(img,patStorage,thresh,1),patPoint);

		//Create pattern image
		IplImage* patImage = cvCreateImage(cvSize(tpl.image[0]->width,tpl.image[1]->height),IPL_DEPTH_8U,3);
		patImage->origin = 1;
		
		if(pattern) {
			cvarInvertPerspective(img,patImage,patPoint,patPointSrc);
			int orient = cvarGetOrientation(patImage,tpl,0,matchThresh);

			//Map orientation
			if(orient) {
				switch(orient) {
				case 4:
					cvarRotSquare(points,2);
					break;
				case 2:
					cvarRotSquare(points,4);
				}
				res = 1;
			}
			
			//Debug
			if(AC_CV_DEBUG) {
				cvNamedWindow("output_d",1);
				cvShowImage("output_d",patImage);
				cvNamedWindow("template_d",1);
				cvShowImage("template_d",tpl.image[0]);
			}
		}
		
		cvReleaseImage(&patImage);
	}
	
	cvReleaseMemStorage(&storage);
	cvReleaseMemStorage(&patStorage);
	return res;
}


/****************
AR
******************/

CvarAr::CvarAr() {
	state = 0;
	flow = new CvarOpticalFlow();
};

CvarAr::~CvarAr() {
	delete flow;
}

/**
 * Load template file
 */
int CvarAr::LoadTemplate(char* filename) {
	return cvarLoadTemplate(&marker,filename);
}

/**
 * Load camera parameter
 */
int CvarAr::LoadCamera(char* filename) {
	return cvarReadCamera(filename,&camera);
}

/**
 * Resize camera parameter based on the camera size
 */
void CvarAr::ResizeCamera(int width,int height) {
	cvarCameraScale(&camera,width,height);
}

/** 
 * Get OpenGL projection matrix
 */
void CvarAr::GetGlProjection(double* projection) {
	cvarCameraProjection(&camera,projection);
	acMatrixTransposed(projection);
}

/**
 * Detect the marker and get the model view matrix for OpenGL
 */
void CvarAr::DetectMarker(unsigned char* imageData,int width,int height,double* modelview,int thresh,double matchThresh) {
	IplImage* arImage = cvCreateImage(cvSize(width,height),IPL_DEPTH_8U,3);
	arImage->origin = 1;
	memcpy(arImage->imageData,imageData,width*height*3);
	
	CvPoint2D32f points[4] = {0};
	if(state==0) {
		//Binarise
		//cvThreshold(arImage,arImage,thresh,255,CV_THRESH_BINARY);
		
		state = cvarArRegistration(arImage,points,marker,thresh,matchThresh);
		if(state)
			flow->Init(arImage,4,points);
	}
	else {
		int res = flow->Update(arImage,4,points);
		int match = 0;
		match = cvarCompareSquare(arImage,points);
		if(res!=4 || match!=4) {
			flow->Destroy();
			state=0;
		}
	}
	
	cvReleaseImage(&arImage);
	
	cvarSquareToMatrix(points,&camera,modelview);
}

void CvarAr::DetectMarkerLight(unsigned char* imageData,int width,int height,
	double *modelview,
	int thresh,double matchThresh) {
	
	IplImage* img = cvCreateImage(cvSize(width,height),IPL_DEPTH_8U,3);
	img->origin=1;
	IplImage* grey = cvCreateImage(cvSize(width,height),IPL_DEPTH_8U,1);
	img->origin=1;
	
	memcpy(img->imageData,imageData,width*height*3);
	
	cvCvtColor(img,grey,CV_BGR2GRAY);
	cvThreshold(grey,grey,thresh,255,CV_THRESH_BINARY);
	
	cvCvtColor(grey,img,CV_GRAY2BGR);
	
	DetectMarker((unsigned char*)img->imageData,width,height,modelview,thresh,matchThresh);
	
	cvReleaseImage(&img);
	cvReleaseImage(&grey);
}

/**
 * Get the detected state
 * \return	0 if no marker detected, else 1.
 */
int CvarAr::GetState() {
	return state;
}

CvarCamera* CvarAr::GetCamera() {
	return &camera;
}

CvarTemplate* CvarAr::getTemplate() {
	return &marker;
}

/*************
Debug
************/
void cvarEnableDebug() {
	AC_CV_DEBUG = 1;
}

/*************
Multiple marker registration
***********/

CvRect cvarSquare2Rect(CvPoint2D32f pt[4]) {
	int x=50000,y=50000,w,h;
	int x2 = -50000, y2 = -50000;
	for(int i=0;i<4;i++) {
		if(pt[i].x < x)
			x=pt[i].x;
		if(pt[i].x > x2)
			x2 = pt[i].x;
		if(pt[i].y < y)
			y = pt[i].y;
		if(pt[i].y > y2)
			y2 = pt[i].y;
	}
	w = x2 - x;
	h = y2 - y;
	return cvRect(x,y,w,h);
}

int cvarGetAllSquares(CvSeq* squares,vector<CvPoint2D32f>* pts) {
	CvSeqReader reader;
	
	int res = 0;
	
	cvStartReadSeq(squares,&reader,0);
	
	for(int i=0;i<squares->total;i+=4) {
		CvPoint pt[4];
		
		CV_READ_SEQ_ELEM(pt[0],reader);
		CV_READ_SEQ_ELEM(pt[1],reader);
		CV_READ_SEQ_ELEM(pt[2],reader);
		CV_READ_SEQ_ELEM(pt[3],reader);
		
		for(int j=0;j<4;j++) {
			CvPoint2D32f pt32;
			pt32.x = pt[j].x;
			pt32.y = pt[j].y;
			pts->push_back(pt32);
		}
		
		res++;
	}
	
	return res;
}

int cvarArMultRegNoTrack(IplImage* img,vector<CvarMarker>* vMarker,vector<CvarTemplate> vTpl,CvarCamera* cam,int thresh,double matchThresh) {
	CvMemStorage* patStorage = cvCreateMemStorage();
	
	//Greyscaling
	IplImage* grey = cvCreateImage(cvSize(img->width,img->height),8,1);
	cvCvtColor(img,grey,CV_BGR2GRAY);
	//cvThreshold(grey,grey,100,255,CV_THRESH_BINARY);
	cvCvtColor(grey,img,CV_GRAY2BGR);
	cvReleaseImage(&grey);
	
	
	
	//Find all squares
	CvMemStorage* squareStorage = cvCreateMemStorage();
	vector<CvPoint2D32f> vPts;
	int nSquare = cvarGetAllSquares(cvarFindSquares(img,squareStorage,thresh),&vPts);

	
	CvPoint2D32f points[4]; //For calculation
	
	vector<CvarMarker> vMarker2; //For calculation
	
	//For each square, check the pattern
	for(int i=0;i<nSquare;i++) {
		
		//Get the subimage from the square
		//Vector to array
		//CvPoint2D32f arrPt[4];
		for(int j=0;j<4;j++) {
			points[j] = vPts[i*4+j];
			//points[j] = arrPt[j];
		}
		CvRect rect = cvarSquare2Rect(points);
		
		cvSetImageROI(img,rect);
		IplImage* crop = cvCreateImage(cvGetSize(img),img->depth,img->nChannels);
		crop->origin = 1;
		cvCopy(img,crop);
		cvResetImageROI(img);
		
		
		//Get pattern from within the square
		int pattern = 0;
		CvPoint2D32f patPoint[4] = {0};
		
		pattern = cvarGetSquare(crop,cvarFindSquares(crop,patStorage,thresh,1),patPoint);
		
		//Create pattern image
		IplImage* patImage = cvCreateImage(cvGetSize(vTpl[0].image[0]),8,3);
		patImage->origin = 1;
		
		
		if(pattern) {
			
			//For every template
			for(int j=0;j<vTpl.size();j++) {
				CvarMarker marker = {0}; //Important to initialise especially "match"
				marker.id = i;
				
				int res = 0;
				
				CvPoint2D32f patPointSrc[4];
				cvarSquare(patPointSrc,vTpl[j].image[j]->width,vTpl[j].image[j]->height,1);
		

				cvarInvertPerspective(crop,patImage,patPoint,patPointSrc);
				
				int orient = cvarGetOrientation(patImage,vTpl[j],&marker.match,matchThresh);
				//printf("%d,%d: %f\n",i,j,marker.match);
				
				marker.tpl = j;
				
				if(orient) {
					switch(orient) {
					case 4:
						cvarRotSquare(points,2);
						break;
					case 2:
						cvarRotSquare(points,4);
					}
					
					res = 1;
				}
				
				//If matched and get orientation, now compare the matched value
				if(res) {
					//Matrix calculation
					memcpy(marker.square,points,4*sizeof(CvPoint2D32f));
					
					//Add the marker info
					vMarker2.push_back(marker);
	
				}
				
				if(0) {
					cvNamedWindow("output_d",1);
					cvShowImage("output_d",patImage);
					cvNamedWindow("template_d",1);
					cvShowImage("template_d",vTpl[j].image[0]);
				}

			}

		}
		
		cvReleaseImage(&patImage);
		cvReleaseImage(&crop);
		


	}
	
	//Process detected marker
	//double matchVal=matchThresh;
	for(int i=0;i<vMarker2.size();i++) {
		//Compare with the other
		//printf("%d, id = %d, tpl = %d, match = %f\n",i,vMarker2[i].id,vMarker2[i].tpl,vMarker2[i].match);
		for(int j=0;j<i;j++) {
			//If same detected marker, and same template, only one will survive
			if(vMarker2[i].id == vMarker2[j].id || vMarker2[i].tpl == vMarker2[j].tpl) {
				if(vMarker2[i].match > vMarker2[j].match)
					vMarker2[j].id = -1;
				else 
					vMarker2[i].id = -1;
			}
		}
	}
	
	//To output
	for(int i=0;i<vMarker2.size();i++) {
		if(vMarker2[i].tpl >= 0 && vMarker2[i].id >= 0) {
			//printf("out: %d, id = %d, tpl = %d, match = %f\n",i,vMarker2[i].id,vMarker2[i].tpl,vMarker2[i].match);
			cvarSquareToMatrix(vMarker2[i].square,cam,vMarker2[i].modelview);
			vMarker->push_back(vMarker2[i]);
		}
	}

	cvReleaseMemStorage(&patStorage);
	cvReleaseMemStorage(&squareStorage);
	//if(vMarker->size() > 1)
		//exit(0);
	return vMarker->size();
}


int cvarTrack(CvPoint2D32f pt1[4],CvPoint2D32f pt2[4]) {
	//Match the point direction
	for(int j=0;j<4;j++) { //4 direction
		int res = 0;
		for(int i=0;i<4;i++) {
			AcPointf tPoint1;
			tPoint1.x = pt1[i].x;
			tPoint1.y = pt1[i].y;
			
			AcPointf tPoint2;
			tPoint2.x = pt2[(i+j)%4].x;
			tPoint2.y = pt2[(i+j)%4].y;
			
			if(acCalcLength(tPoint1,tPoint2) < 20) {
				res++;
			}
		}
		if(res == 4) {
			for(int i=0;i<4;i++) {
				pt1[i] = pt2[(i+j)%4];
			}
			return 1;
		}
	}
	return 0;
}

int cvarArMultRegistration(IplImage* img,vector<CvarMarker>* vMarker,vector<CvarTemplate> vTpl,CvarCamera* cam,int thresh,double matchThresh) {
	//Auto thresholding
	int autothresh = 0;
	if(thresh == AC_THRESH_AUTO) {
		autothresh = 1;
		if(g_needRandom) {
			if(g_isRandom == 0) {
				srand(time(NULL));
				g_isRandom = 1;
			}
			g_thresh = rand()%256;
		}
		thresh = g_thresh;
	}
	
	CvMemStorage* patStorage = cvCreateMemStorage();
	
	//Greyscaling
	IplImage* grey = cvCreateImage(cvSize(img->width,img->height),8,1);
	cvCvtColor(img,grey,CV_BGR2GRAY);
	//cvThreshold(grey,grey,100,255,CV_THRESH_BINARY);
	cvCvtColor(grey,img,CV_GRAY2BGR);
	cvReleaseImage(&grey);
	
	
	
	//Find all squares
	CvMemStorage* squareStorage = cvCreateMemStorage();
	vector<CvPoint2D32f> vPts;
	int nSquare = cvarGetAllSquares(cvarFindSquares(img,squareStorage,thresh,0),&vPts);
	

	//Checking for previous marker square
	vector<int> reserve; //For reserving the previous data

	for(int i=0;i<vMarker->size();i++ ) {
		for(int j=0;j<vPts.size();j+=4) {
			//Points to array
			CvPoint2D32f arrPoint[4];
			for(int k=0;k<4;k++) {
				arrPoint[k] = vPts[j+k];
			}
			
			//If current is related to previous,
			// that means the previous one can be use, and the current one can be eliminated
			if(cvarTrack((*vMarker)[i].square,arrPoint)) {
				reserve.push_back(i);
				
				//Directly remove it from the vector, because removing is difficult
				vPts.erase(vPts.begin()+j,vPts.begin()+j+4); //Remove the 4 vertices
				
				//Recalculate the modelview
				cvarSquareToMatrix((*vMarker)[i].square,cam,(*vMarker)[i].modelview,(*vMarker)[i].ratio);
			}
		}
	}
	
	//Make a copy of previous data
	vector<CvarMarker> cpy = *vMarker;
	vMarker->clear();
	

	//Store only the updated
	for(int i=0;i<reserve.size();i++) {
		vMarker->push_back(cpy[reserve[i]]);
	}
	
		
	//For template matching part
	CvPoint2D32f points[4]; //For calculation
	
	vector<CvarMarker> vMarker2; //For calculation
	
	//For each square, check the pattern
	for(int i=0;i<vPts.size()/4;i++) {
		
		//Get the subimage from the square
		//Vector to array
		//CvPoint2D32f arrPt[4];
		for(int j=0;j<4;j++) {
			points[j] = vPts[i*4+j];
			//points[j] = arrPt[j];
		}
		CvRect rect = cvarSquare2Rect(points);
		rect.x-=5;
		rect.y-=5;
		rect.width+=10;
		rect.height+=10;
		
		cvSetImageROI(img,rect);
		IplImage* crop = cvCreateImage(cvGetSize(img),img->depth,img->nChannels);
		crop->origin = 1;
		cvCopy(img,crop);
		cvResetImageROI(img);
		
		
		//Get pattern from within the square
		int pattern = 0;
		CvPoint2D32f patPoint[4] = {0};
		
		//For every template
		for(int j=0;j<vTpl.size();j++) {
			
			//Different algorithm
			if(vTpl[j].type == 0) //Find inner white square
				pattern = cvarGetSquare(crop,cvarFindSquares(crop,patStorage,thresh,1),patPoint);
			else if(vTpl[j].type != 0) //Find outer black square
				pattern = cvarGetSquare(crop,cvarFindSquares(crop,patStorage,thresh,0),patPoint);
		
			if(pattern) {
				//Create pattern image
				IplImage* patImage;
				if(vTpl[j].type !=2) //Not ARTag, ARTag must be 10x10
					patImage = cvCreateImage(cvGetSize(vTpl[j].image[0]),8,3);
				else //ARTag
					patImage = cvCreateImage(cvSize(10,10),8,3);

				patImage->origin = 1;
				
	
				CvarMarker marker = {0}; //Important to initialise especially "match"
				marker.id = i;
				
				int res = 0;
				
				CvPoint2D32f patPointSrc[4];
				
				//Different algorithm
				if(vTpl[j].type==0) { //ARToolKit
					cvarSquare(patPointSrc,vTpl[j].image[j]->width,vTpl[j].image[j]->height,1);
				}
				else if(vTpl[j].type == 2) { //ARTag
					cvarSquare(patPointSrc,10,10,0);
				}
				else //acAR
					cvarSquare(patPointSrc,vTpl[j].image[j]->width,vTpl[j].image[j]->height,0);
		

				cvarInvertPerspective(crop,patImage,patPoint,patPointSrc);
									

				int orient;
				if(vTpl[j].type != 2) { //Non-ARTag
					orient = cvarGetOrientation(patImage,vTpl[j],&marker.match,matchThresh);
					//printf("%d,%d: %f\n",i,j,marker.match);
				}
				else {
					//Crop
					CvRect croptag = cvRect(1,1,8,8);
					cvSetImageROI(patImage,croptag);
					
					//Binarise
					IplImage* patImageg = cvCreateImage(cvGetSize(patImage),8,1);
					cvCvtColor(patImage,patImageg,CV_BGR2GRAY);
					cvThreshold(patImageg,patImageg,thresh,1,CV_THRESH_BINARY);
					
					//Image to bit
					long long int bit;
					acArray2DToBit((unsigned char*)patImageg->imageData,patImageg->width,patImageg->height,&bit);
					
					//Get orientation of the bit
					orient=0;
					for(int k=0;k<4;k++) {
						if(bit == vTpl[j].code[k])
							orient = k+1;
					}
					
					//Match
					if(orient)
						marker.match = 1; //So that it is the best
					else
						marker.match = 0;
					
					
					//cvThreshold(patImageg,patImageg,0.5,255,CV_THRESH_BINARY);
					//cvCvtColor(patImageg,patImage,CV_GRAY2BGR);
					
					//Release
					cvReleaseImage(&patImageg);
					cvResetImageROI(patImage);

				}
				
				//Record the current template so that later will be pushed into vector
				marker.tpl = j;
				
				if(orient) {
					switch(orient) {
					case 4:
						cvarRotSquare(points,2);
						break;
					case 2:
						cvarRotSquare(points,4);
					}
					
					res = 1;
				}
			
				
				//If matched and get orientation, now compare the matched value
				if(res) {
					//Matrix calculation
					memcpy(marker.square,points,4*sizeof(CvPoint2D32f));
					
					//Add in ratio
					if(vTpl[j].type != 2) //Non ARTag
						marker.ratio = (float)(vTpl[j].image[0]->width) / vTpl[j].image[0]->height;
					else
						marker.ratio = 1;
					
					//Add the marker info
					vMarker2.push_back(marker);
	
				}
				
				if(AC_CV_DEBUG && vTpl[j].type != 2) {
					cvNamedWindow("output_d",1);
					cvShowImage("output_d",patImage);
					cvNamedWindow("template_d",1);
					cvShowImage("template_d",vTpl[j].image[0]);
				}
				
				
				cvReleaseImage(&patImage);

			}

		}
		

		cvReleaseImage(&crop);
		


	}
	
	//Process detected marker
	//double matchVal=matchThresh;
	for(int i=0;i<vMarker2.size();i++) {
		//Compare with the other
		//printf("%d, id = %d, tpl = %d, match = %f\n",i,vMarker2[i].id,vMarker2[i].tpl,vMarker2[i].match);
		for(int j=0;j<i;j++) {
			//If same detected marker, and same template, only one will survive
			if(vMarker2[i].id == vMarker2[j].id || vMarker2[i].tpl == vMarker2[j].tpl) {
				if(vMarker2[i].match > vMarker2[j].match)
					vMarker2[j].id = -1;
				else 
					vMarker2[i].id = -1;
			}
		}
	}
	
	//To output
	for(int i=0;i<vMarker2.size();i++) {
		if(vMarker2[i].tpl >= 0 && vMarker2[i].id >= 0) {
			//printf("out: %d, id = %d, tpl = %d, match = %f\n",i,vMarker2[i].id,vMarker2[i].tpl,vMarker2[i].match);
			cvarSquareToMatrix(vMarker2[i].square,cam,vMarker2[i].modelview,vMarker2[i].ratio);
			vMarker->push_back(vMarker2[i]);
		}
	}

	cvReleaseMemStorage(&patStorage);
	cvReleaseMemStorage(&squareStorage);
	
	
	//Auto threshold
	if(autothresh) {
		if(vMarker->size()) {
			g_needRandom = 0;
		}
		else {
			g_needRandom = 1;
		}
	}
	
	return vMarker->size();
}


/////////////// 2010-05-03
/**
 * \brief Convert RGB to Gray
 *
 * This function is written because of unknown problem for the function cvCvtColor() in grayscale
 */
void cvarRgb2Gray(IplImage* src,IplImage* dst) {
	for(int i=0;i<src->height;i++) {
		for(int j=0;j<src->width;j++) {
			dst->imageData[i*src->width+j] = src->imageData[(i*src->width+j)*3+0] * 0.299 +
				src->imageData[(i*src->width+j)*3+1] * 0.587 +
				src->imageData[(i*src->width+j)*3+2] * 0.114;
		}
	}
}

//////////////
void acSkinSegmentation(unsigned char* imageData,int width,int height) {
	for(int i=0;i<width * height *3;i+=3) {
		int r,g,b;
		r = imageData[i];
		g = imageData[i+1];
		b = imageData[i+2];
		
		int max = r;
		if(g>max)
			max = g;
		if(b>max)
			max = b;
		
		int min = r;
		if(g<min)
			min = g;
		if(b<min)
			min = b;
		
		if(r > 95 && g > 40 && b > 20 &&
			(max - min > 15) &&
			(abs(r - g) > 15) &&
			r > g && r > b) 
		{
		}
		else {
			imageData[i] = imageData[i+1] = imageData[i+2] = 0;
		}
	}
}

void acFingerTracking(IplImage* finger,unsigned char* data,int width,int height,int* x,int* y,int draw) {
	//For skin segmentation
	IplImage* img = cvCreateImage(cvSize(width,height),IPL_DEPTH_8U,3);
	img->origin = 1;
	memcpy(img->imageData,data,width*height*3);

	//For drawing the cursor
	IplImage* cur = cvCloneImage(img);
	
	cvCvtColor(img,img,CV_BGR2RGB);
	acSkinSegmentation((unsigned char*)img->imageData,img->width,img->height);
	cvCvtColor(img,img,CV_RGB2BGR);
	
	IplImage* result = cvCreateImage(cvSize(img->width - finger->width+1,
		img->height - finger->height +1),
		IPL_DEPTH_32F,1);
	result->origin=1;
	
	cvMatchTemplate(img,finger,result,CV_TM_CCORR_NORMED);
	double minval,maxval;
	CvPoint minloc,maxloc;
	cvMinMaxLoc(result,&minval,&maxval,&minloc,&maxloc);
	
	int ret = 0;
	*x = *y = -1;
	if(maxval>0.6) {
		//cvCircle(result,maxloc,1,cvScalar(0,255,0),5);
		
		maxloc.x += (float)finger->width/2;
		maxloc.y += (float)finger->height/2;
		
		if(draw) {
			cvCircle(cur,maxloc,1,cvScalar(0,255,0),5);
			memcpy(data,cur->imageData,cur->imageSize);
		}
		*x = maxloc.x;
		*y = maxloc.y;
		ret = 1;
	}
	
	
	cvReleaseImage(&img);
	cvReleaseImage(&cur);
	cvReleaseImage(&result);

}


//////////////////SURF

static double compareSURFDescriptors( const float* d1, const float* d2, double best, int length )
{
    double total_cost = 0;
    assert( length % 4 == 0 );
    for( int i = 0; i < length; i += 4 )
    {
        double t0 = d1[i] - d2[i];
        double t1 = d1[i+1] - d2[i+1];
        double t2 = d1[i+2] - d2[i+2];
        double t3 = d1[i+3] - d2[i+3];
        total_cost += t0*t0 + t1*t1 + t2*t2 + t3*t3;
        if( total_cost > best )
            break;
    }
    return total_cost;
}


static int naiveNearestNeighbor( const float* vec, int laplacian,
                      const CvSeq* model_keypoints,
                      const CvSeq* model_descriptors )
{
    int length = (int)(model_descriptors->elem_size/sizeof(float));
    int i, neighbor = -1;
    double d, dist1 = 1e6, dist2 = 1e6;
    CvSeqReader reader, kreader;
    cvStartReadSeq( model_keypoints, &kreader, 0 );
    cvStartReadSeq( model_descriptors, &reader, 0 );

    for( i = 0; i < model_descriptors->total; i++ )
    {
        const CvSURFPoint* kp = (const CvSURFPoint*)kreader.ptr;
        const float* mvec = (const float*)reader.ptr;
    	CV_NEXT_SEQ_ELEM( kreader.seq->elem_size, kreader );
        CV_NEXT_SEQ_ELEM( reader.seq->elem_size, reader );
        if( laplacian != kp->laplacian )
            continue;
        
        //if(kp->size<10)
        	//continue;
        
        d = compareSURFDescriptors( vec, mvec, dist2, length );
        if( d < dist1 )
        {
            dist2 = dist1;
            dist1 = d;
            neighbor = i;
        }
        else if ( d < dist2 )
            dist2 = d;
    }
    if ( dist1 < 0.6*dist2 )
        return neighbor;
    return -1;
}

static void findPairs( const CvSeq* objectKeypoints, const CvSeq* objectDescriptors,
           const CvSeq* imageKeypoints, const CvSeq* imageDescriptors, vector<int>& ptpairs )
{
    int i;
    CvSeqReader reader, kreader;
    cvStartReadSeq( objectKeypoints, &kreader );
    cvStartReadSeq( objectDescriptors, &reader );
    ptpairs.clear();

    for( i = 0; i < objectDescriptors->total; i++ )
    {
        const CvSURFPoint* kp = (const CvSURFPoint*)kreader.ptr;
        const float* descriptor = (const float*)reader.ptr;
        CV_NEXT_SEQ_ELEM( kreader.seq->elem_size, kreader );
        CV_NEXT_SEQ_ELEM( reader.seq->elem_size, reader );

        //if(kp->size<20)
        	//continue;
 
        int nearest_neighbor = naiveNearestNeighbor( descriptor, kp->laplacian, imageKeypoints, imageDescriptors );
        if( nearest_neighbor >= 0 )
        {
            ptpairs.push_back(i);
            ptpairs.push_back(nearest_neighbor);
        }
    }
}


/* a rough implementation for object location */
static int locatePlanarObject( const CvSeq* objectKeypoints, const CvSeq* objectDescriptors,
                    const CvSeq* imageKeypoints, const CvSeq* imageDescriptors,
                    const CvPoint src_corners[4], CvPoint dst_corners[4] )
{
    double h[9];
    CvMat _h = cvMat(3, 3, CV_64F, h);
    vector<int> ptpairs;
    vector<CvPoint2D32f> pt1, pt2;
    CvMat _pt1, _pt2;
    int i, n;

    findPairs( objectKeypoints, objectDescriptors, imageKeypoints, imageDescriptors, ptpairs );

    n = ptpairs.size()/2;
    if( n < 4 )
        return 0;

    pt1.resize(n);
    pt2.resize(n);
    for( i = 0; i < n; i++ )
    {
        pt1[i] = ((CvSURFPoint*)cvGetSeqElem(objectKeypoints,ptpairs[i*2]))->pt;
        pt2[i] = ((CvSURFPoint*)cvGetSeqElem(imageKeypoints,ptpairs[i*2+1]))->pt;
    }

    _pt1 = cvMat(1, n, CV_32FC2, &pt1[0] );
    _pt2 = cvMat(1, n, CV_32FC2, &pt2[0] );
    cout<<100<<endl;
    if( !cvFindHomography( &_pt1, &_pt2, &_h, CV_LMEDS, 5 )) {
    	cout<<400<<endl;
        return 0;
    }
    cout<<200<<endl;

    for( i = 0; i < 4; i++ )
    {
        double x = src_corners[i].x, y = src_corners[i].y;
        double Z = 1./(h[6]*x + h[7]*y + h[8]);
        double X = (h[0]*x + h[1]*y + h[2])*Z;
        double Y = (h[3]*x + h[4]*y + h[5])*Z;
        dst_corners[i] = cvPoint(cvRound(X), cvRound(Y));
    }

    return 1;
}

void cvarSurfMatch(IplImage *image,IplImage* object,
	CvSeq *objectKeypoints,CvSeq *objectDescriptors,
	CvarCamera* cam,double* modelview,
	IplImage* result) 
{
	float ratio = (float)object->width / object->height;

	CvMemStorage *imgStorage = cvCreateMemStorage();
    CvSeq *imageKeypoints = 0, *imageDescriptors = 0;

    CvSURFParams imgParam = cvSURFParams(500,1);
    cvExtractSURF( image, 0, &imageKeypoints, &imageDescriptors, imgStorage, imgParam );

    
    CvRect rect = cvGetImageROI(image);
    
    CvPoint src_corners[4] = { {0,0},{object->width,0},
    	{object->width,object->height},
    	{0,object->height}};
    CvPoint dst_corners[4];
    
    if(locatePlanarObject(objectKeypoints,objectDescriptors,imageKeypoints,imageDescriptors,
    	src_corners,dst_corners)) {
		
    	CvMat* objPoint = cvCreateMat(4,3,CV_64F);
		CvMat* imgPoint = cvCreateMat(4,2,CV_64F);
		
		
    
    	for(int i=0;i<4;i++) {
    		
			CvPoint r1 = dst_corners[i%4];
			CvPoint r2 = dst_corners[(i+1)%4];
			cvLine(result,cvPoint(r1.x,r1.y),
				cvPoint(r2.x,r2.y),cvScalar(255,0,0));
			
			objPoint->data.db[i*3] = ((2*(src_corners[i].x/object->width))-1)*ratio;
			objPoint->data.db[i*3+1] = (2*(src_corners[i].y/object->height))-1;
			objPoint->data.db[i*3+2] = 0;
			imgPoint->data.db[i*2] = r1.x;
			imgPoint->data.db[i*2+1] = r1.y;
			
			//printf("%d %d %d %d\n",r1.x,r1.y,r2.x,r2.y);
			//printf("data %f %f\n",objPoint->data.db[i*2],objPoint->data.db[i*2+1]);

		}
		
		cvarFindCamera(cam,objPoint,imgPoint,modelview);
		cvReleaseMat(&objPoint);
		cvReleaseMat(&imgPoint);
    }//*/
    
   
    
    /*vector<int> ptpairs;
    findPairs( objectKeypoints, objectDescriptors, imageKeypoints, imageDescriptors, ptpairs );
    //flannFindPairs( objectKeypoints, objectDescriptors, imageKeypoints, imageDescriptors, ptpairs );
    if(ptpairs.size()>=8) {
		CvMat* objPoint = cvCreateMat(ptpairs.size()/2,3,CV_64F);
		CvMat* imgPoint = cvCreateMat(ptpairs.size()/2,2,CV_64F);
		
		for(int i = 0; i <ptpairs.size(); i += 2 )
		{
			CvSURFPoint* r1 = (CvSURFPoint*)cvGetSeqElem( objectKeypoints, ptpairs[i] );
			CvSURFPoint* r2 = (CvSURFPoint*)cvGetSeqElem( imageKeypoints, ptpairs[i+1] );
			
			
			objPoint->data.db[i/2*3] = ((2*(r1->pt.x/object->width))-1)*ratio; //Translate to centre
			objPoint->data.db[i/2*3+1] = (2*(r1->pt.y/object->height))-1; //Translate to centre
			objPoint->data.db[i/2*3+2] = 0;
			imgPoint->data.db[i] = r2->pt.x + rect.x;
			imgPoint->data.db[i+1] = r2->pt.y + rect.y;
			
			cvCircle(result,cvPoint(cvRound(r2->pt.x),cvRound(r2->pt.y)),
				cvRound(r2->size*1.2/9.*2),cvScalar(255,255,255),1,8,0);
			//printf("size1: %d\n",r1->size);
			//printf("size2: %d\n",r2->size);
		}
		
		//cvarFindCamera(cam,objPoint,imgPoint,modelview);
		cvReleaseMat(&objPoint);
		cvReleaseMat(&imgPoint);
    }//*/

    cvReleaseMemStorage(&imgStorage);

}

