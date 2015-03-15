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

#include <iostream>
#include <fstream>
#include <vector>
#include <time.h>

// opengl
#include <GL/glut.h>

// opencv
#include "cv.h"
#include "highgui.h"

// opencv-ar
#include "opencvar.h"
#include "acgl.h"
#include "acmath.h"

using namespace std;

CvCapture* g_cap; //Video capturing
CvarCamera g_cam; //The global camera data

float g_projection[16];
double  g_modelview[16];

vector<CvarTemplate> g_vtpl;	// Vector of AR template
vector<CvarMarker> g_marker;	// Vector of Detected Marker

int g_lastdetect = 0;
int g_currentWindow,g_originalWindow;

IplImage *g_image,*g_object;

int g_bTeapot = 1;

void display()
{
	// get a frame
	IplImage* frame = cvQueryFrame(g_cap);
	// flip array by x-axis.
	cvFlip(frame,frame);	
	
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	glColor3f(1,1,1);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);

	glPushMatrix();

	acGlTextureProject((unsigned char*)frame->imageData,frame->width,frame->height,frame->nChannels,1);
	glClear(GL_DEPTH_BUFFER_BIT);
	
	//AR detection
	GLdouble modelview[16] = {0};	
	//Detect marker
	int ar_detect = cvarArMultRegistration(frame,&g_marker,g_vtpl,&g_cam,AC_THRESH_AUTO,0.94);
	
	glMatrixMode(GL_MODELVIEW);

	glPushMatrix();
	
	int test=0;
	double testmodel[16] = {0};
	for(int i=0;i<ar_detect;i++) {
		
		glEnable(GL_LIGHTING);
		glEnable(GL_LIGHT0);
			
		if(g_marker[i].tpl == 0) {
			
			glLoadMatrixd(g_marker[i].modelview);
			glRotatef(90,1,0,0);
			glTranslatef(0,0.5,0);
			
			printf("%d:\n",g_marker[i].tpl);
			for(int j=0;j<4;j++) {
				for(int k=0;k<4;k++) {
					printf("%f\t",g_marker[i].modelview[j*4+k]);
				}
				printf("\n");
			}
			printf("\n");

			if(g_bTeapot)
				glutSolidTeapot(1);
	
			//glGetFloatv(GL_PROJECTION_MATRIX,g_projection);
			//glGetFloatv(GL_MODELVIEW_MATRIX,g_modelview);
		}
		else if(g_marker[i].tpl == 1) {
			
			glLoadMatrixd(g_marker[i].modelview);
			glRotatef(90,1,0,0);
			glTranslatef(0,0.5,0);
			glutSolidCube(1);
			
		}
		else if(g_marker[i].tpl == 2) {
			glLoadMatrixd(g_marker[i].modelview);
			//glRotatef(90,1,0,0);
			glTranslatef(0,0,0.5);
			glutSolidSphere(1,8,8);
		}
		else if(g_marker[i].tpl == 3) {
			glLoadMatrixd(g_marker[i].modelview);
			//glRotatef(90,1,0,0);
			glTranslatef(0,0,0.5);
			glutSolidTorus(0.5,1,8,12);
			printf("%d:\n",g_marker[i].tpl);
			for(int j=0;j<4;j++) {
				for(int k=0;k<4;k++) {
					printf("%f\t",g_marker[i].modelview[j*4+k]);
				}
				printf("\n");
			}
			printf("\n");
			test = 1;
			memcpy(testmodel,g_marker[i].modelview,16*sizeof(double));
			
		}
		else if(g_marker[i].tpl == 4) {
			glLoadMatrixd(g_marker[i].modelview);
			glTranslatef(0,0,0.5);
			glutWireTorus(0.5,1,8,12);
			printf("%d:\n",g_marker[i].tpl);
			for(int j=0;j<4;j++) {
				for(int k=0;k<4;k++) {
					printf("%f\t",g_marker[i].modelview[j*4+k]);
				}
				printf("\n");
			}
			printf("\n");
			
			//Generate another 
			if(test ==1) {
				for(int j=0;j<4;j++) {
					for(int k=0;k<4;k++) {
						testmodel[j*4+k] += g_marker[i].modelview[j*4+k];
						testmodel[j*4+k] /= 2;
					}
				}
			}
			glLoadMatrixd(testmodel);
			glTranslatef(0,0,0.5);
			glutWireTorus(0.5,1,8,12);
		}//*/
		
		glDisable(GL_LIGHTING);
		
	}

	glPopMatrix();

	glPopMatrix();

	glutSwapBuffers();
}


void reshape(int w,int h)
{
	glViewport(0,0,w,h);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	double projection[16];
	cvarCameraProjection(&g_cam,projection);
	
	acMatrixTransposed(projection);
	glLoadMatrixd(projection);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

void idle()
{
	// no operate
	// re-draw image directly
	glutPostRedisplay();
}

void visible(int vis) {
	if(vis == GLUT_VISIBLE) {
		// callback function for callback idle
		glutIdleFunc(idle);
	}
	else
		glutIdleFunc(NULL);
}

void keyboard(unsigned char key,int x,int y)
{
	switch(key)
	{
	case 27:
		cvReleaseCapture(&g_cap);
		exit(0);
		break;
	case 'a':	//Turn off the teapot
		g_bTeapot = !g_bTeapot;
		break;
	}
}

void mouse(int button,int state,int x,int y) {
	if(button==GLUT_LEFT_BUTTON && state==GLUT_DOWN) {

	}
}


int main(int argc,char** argv) {

	// Camera Perpare
	g_cap = cvCreateCameraCapture(0);
	if(!g_cap) {
		fprintf(stderr,"Create camera capture failed\n");
		return 1;
	}
	
	// -- get templates --
	// all store in g_vtpl

	CvarTemplate tpl;
	cvarLoadTemplateTag(&tpl,"actag.png");
	//cvarLoadTag(&tpl,0x49a99b1d19aaaa44LL); //Using code
	g_vtpl.push_back(tpl);
	
	CvarTemplate tpl2;
	cvarLoadTemplate(&tpl2,"aclib.png",1);
	g_vtpl.push_back(tpl2);

	// Initial g_cam
	IplImage* frame = cvQueryFrame(g_cap);
	cvarReadCamera(NULL,&g_cam);
	cvarCameraScale(&g_cam,frame->width,frame->height);	
	
	// Initial opengl
	glutInit(&argc,argv);
	glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB);
	glutInitWindowSize(640,480);

	glutCreateWindow("MyGLUT");
	
	//Callback
	glutDisplayFunc(display);	// if the app is display, callback the display function.
	glutReshapeFunc(reshape);	// if the app's window display area size changed, callback the reshape function.
	glutKeyboardFunc(keyboard); // Handle keyboard events - ASCII key
	glutMouseFunc(mouse);		// Handle mouse events
	glutIdleFunc(idle);			// Handle idle events
	
	glEnable(GL_DEPTH_TEST);

	glutMainLoop();

	cvReleaseCapture(&g_cap);
	return 0;
}


