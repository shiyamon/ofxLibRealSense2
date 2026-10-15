#include "ofApp.h"

int main()
{
	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 2);
	settings.setSize(1600, 900);
	ofCreateWindow(settings);

	ofRunApp(new ofApp());
}
