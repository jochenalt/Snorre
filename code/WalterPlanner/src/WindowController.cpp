
#include <stdio.h>

#include "WindowController.h"
#include "Util.h"
#include "Kinematics.h"
#include "BotView.h"
#include "TrajectorySimulation.h"
#include "uiconfig.h"
#include "ActuatorProperty.h"
#include "logger.h"

using namespace std;

int WindowWidth = 1000;									// initial window size
int WindowHeight = 735;

float startUpDuration = 5000;							// duration of startup animation

// handles of opengl windows and subwindows
int wMain, wMainBotView, wSideBotView, wFrontBotView, wTopBotView;	// window handler of windows

// kinematics widgets
GLUI_Spinner* poseSpinner[7] = {NULL,NULL,NULL, NULL, NULL, NULL, NULL};
bool poseSpinnerINT[7] = {false, false, false, false, false, false, true };
float poseSpinnerLiveVar[7] = {0,0,0, 0,0,0,0};

// Handview widget
GLUI_Spinner* handviewSpinner[3] = {NULL,NULL,NULL};
float handviewSpinnerLiveVar[3] = {0,0,0};

GLUI_Spinner* angleSpinner[NumberOfActuators] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL};
float anglesLiveVar[NumberOfActuators] = {0.0,0.0,0.0,0.0,0.0,0.0,0.0 };
bool angleSpinnerINT[NumberOfActuators] = {false, false, false, false, false, false, true };
GLUI_Spinner* lastActiveSpinner = NULL;

bool kinematicsHasChanged = false; 				// semaphore, true, if current joint angles or pose has changed

// configuration widget
GLUI_Checkbox  *confDirectionCheckbox= NULL;
GLUI_Checkbox  *confgFlipCheckbox= NULL;
GLUI_Checkbox  *configTurnCheckbox= NULL;

int configDirectionLiveVar= 0;					// kinematics configuration, bot looks to the front or to the back
int configFlipLiveVar = 0;						// kinematics triangle flip
int configTurnLiveVar = 0;						// kinematics forearm flip

const int ConfigurationTypeDirectionID = 0;
const int ConfigurationTypeFlipID= 1;
const int ConfigurationViewTurnID= 2;

// each mouse motion call requires a display() call before doing the next mouse motion call. postDisplayInitiated
// is a semaphore that coordinates this across several threads
// (without that, we have so many motion calls that rendering is bumpy)
// postDisplayInitiated is true, if a display()-invokation is pending but has not yet been executed (i.e. allow a following display call)
volatile static bool postDisplayInitiated = true;


void postRedisplay() {
	int saveWindow = glutGetWindow();
	glutSetWindow(wMain);
	postDisplayInitiated = true;
	glutPostRedisplay();
	glutSetWindow(saveWindow );
	if (lastActiveSpinner != NULL)
		lastActiveSpinner->activate(true);

}


void copyAnglesToView() {
	for (int i = 0;i<NumberOfActuators;i++) {
		float value = degrees(TrajectorySimulation::getInstance().getCurrentAngles()[i]);
		value = roundValue(value);
		if (value != anglesLiveVar[i]) {
			angleSpinner[i]->set_float_val(value);
		}
	}

	WindowController::getInstance().mainBotView.setAngles(TrajectorySimulation::getInstance().getCurrentAngles(), TrajectorySimulation::getInstance().getCurrentPose());
}

JointAngles getAnglesView() {
	JointAngles angles;
	for (int i = 0;i<NumberOfActuators;i++) {
		angles[i] = radians(anglesLiveVar[i]);
	}
	return angles;
}


void copyPoseToView() {
	const Pose& tcp = TrajectorySimulation::getInstance().getCurrentPose();
	for (int i = 0;i<7;i++) {
		rational value;
		if (i<3)
			value = tcp.position[i];
		else
			if (i<6)
				value = degrees(tcp.orientation[i-3]);
			else
				value = tcp.gripperDistance;

		value = roundValue(value);
		if (value != poseSpinnerLiveVar[i]) {
			poseSpinner[i]->set_float_val(value); // set only when necessary, otherwise the cursor blinks
		}
	}

	Point tcpOffset = Kinematics::getInstance().getTCPCoordinates();

	handviewSpinner[X]->set_float_val(tcpOffset.x);
	handviewSpinner[Y]->set_float_val(tcpOffset.y);
	handviewSpinner[Z]->set_float_val(tcpOffset.z);

}

Pose getPoseView() {
	Pose tcp;
	for (int i = 0;i<7;i++) {
		float value = poseSpinnerLiveVar[i];
		if (i<3)
			tcp.position[i] = value;
		else
			if (i<6)
				tcp.orientation[i-3] = radians(value);
			else
				tcp.gripperDistance = value;
	}

	return tcp;
}

PoseConfigurationType getConfigurationView() {
	PoseConfigurationType config;
	config.poseDirection = (PoseConfigurationType::PoseDirectionType)(1-configDirectionLiveVar);
	config.poseFlip = (PoseConfigurationType::PoseFlipType)(1-configFlipLiveVar);
	config.poseTurn= (PoseConfigurationType::PoseForearmType)(1-configTurnLiveVar);

	return config;
}


void copyConfigurationToView() {

	PoseConfigurationType config = TrajectorySimulation::getInstance().getCurrentConfiguration();
	confDirectionCheckbox->set_int_val(1-config.poseDirection);
	confgFlipCheckbox->set_int_val(1-config.poseFlip);
	configTurnCheckbox->set_int_val(1-config.poseTurn);

	bool Direction = false, Flip = false, Turn= false;
	std::vector<KinematicsSolutionType> validSolutions = TrajectorySimulation::getInstance().getPossibleSolutions();
	for (unsigned int i = 0;i<validSolutions.size();i++) {
		PoseConfigurationType possibleConfig = validSolutions[i].config;
		if (possibleConfig.poseDirection != config.poseDirection)
			Direction = true;
		if (possibleConfig.poseFlip != config.poseFlip)
			Flip = true;
		if (possibleConfig.poseTurn != config.poseTurn)
			Turn = true;
	}
	if (Direction)
		confDirectionCheckbox->enable();
	else
		confDirectionCheckbox->disable();

	if (Flip)
		confgFlipCheckbox->enable();
	else
		confgFlipCheckbox->disable();

	if (Turn)
		configTurnCheckbox->enable();
	else
		configTurnCheckbox->disable();
}


/* Handler for window-repaint event. Call back when the window first appears and
 whenever the window needs to be re-painted. */
void display() {
	if (!postDisplayInitiated)
		return;

	postDisplayInitiated = false;
	glutSetWindow(wMain);
	glClearColor(glMainWindowColor[0], glMainWindowColor[1], glMainWindowColor[2], 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// copy bot data to view
	copyAnglesToView();
	copyPoseToView();
	copyConfigurationToView();

	WindowController::getInstance().mainBotView.display();

	TrajectoryView::getInstance().display();

	glFlush();  // Render now
	glutSwapBuffers();
}

void nocallback(int value) {
}

/* Called back when timer expired [NEW] */
void StartupTimerCallback(int value) {
	static uint32_t startupTime_ms = millis();
	uint32_t timeSinceStart_ms = millis()-startupTime_ms;
	if (timeSinceStart_ms < startUpDuration) {
		float startupRatio= ((float)(timeSinceStart_ms)/startUpDuration)*PI/2.0;
		WindowController::getInstance().mainBotView.setStartupAnimationRatio(startupRatio);

		// repainting is done in Idle Callback, checking the botModifed flag
		kinematicsHasChanged = true;
		glutTimerFunc(20, StartupTimerCallback, 0);
	}
}

void reshape(int w, int h) {
	int savedWindow = glutGetWindow();
	WindowWidth = w;
	WindowHeight = h;
	glViewport(0, 0, w, h);

	int MainSubWindowWidth = (w -InteractiveWindowWidth - 2 * WindowGap);
	int MainSubWindowHeight = (h - 2 * WindowGap);

	WindowController::getInstance().mainBotView.reshape(WindowGap, WindowGap,MainSubWindowWidth, MainSubWindowHeight);

	glutSetWindow(savedWindow);
}

static int lastMouseX = 0;
static int lastMouseY = 0;
static int lastMouseScroll = 0;
static bool mouseViewPane = false;

static bool mouseBotXZPane = false;
static bool mouseBotYZPane = false;
static bool mouseBotOrientationXYPane = false;
static bool mouseBotOrientationYZPane = false;

void SubWindow3dMotionCallback(int x, int y) {
	// if display has not yet been called after the last motion, dont execute
	// this one, but wait for

	const float slowDownPositionFactor = 1.0/3.0;
	const float slowDownOrientationFactor = 1.0/4.0;

	float diffX = (float) (x-lastMouseX);
	float diffY = (float) (y-lastMouseY);
	if (mouseViewPane) {
		WindowController::getInstance().mainBotView.changeEyePosition(0, -diffX, -diffY);
	} else
	if (mouseBotXZPane) {
		poseSpinner[0]->set_float_val(roundValue(poseSpinnerLiveVar[0] + diffY*slowDownPositionFactor));
		poseSpinner[1]->set_float_val(roundValue(poseSpinnerLiveVar[1] + diffX*slowDownPositionFactor));
		WindowController::getInstance().changedPoseCallback();
	} else
	if (mouseBotYZPane) {
		poseSpinner[2]->set_float_val(roundValue(poseSpinnerLiveVar[2] - diffY*slowDownPositionFactor));
		WindowController::getInstance().changedPoseCallback();
	} else
	if (mouseBotOrientationYZPane) {
		poseSpinner[3]->set_float_val(roundValue(poseSpinnerLiveVar[3] + diffX*slowDownOrientationFactor));
		WindowController::getInstance().changedPoseCallback();
	} else
	if (mouseBotOrientationXYPane) {
		poseSpinner[5]->set_float_val(roundValue(poseSpinnerLiveVar[5] + diffX*slowDownOrientationFactor));
		poseSpinner[4]->set_float_val(roundValue(poseSpinnerLiveVar[4] + diffY*slowDownOrientationFactor));
		WindowController::getInstance().changedPoseCallback();
	} else
		if (lastMouseScroll != 0) {
			WindowController::getInstance().mainBotView.changeEyePosition(-25*lastMouseScroll, 0,0);
			kinematicsHasChanged = true;
			lastMouseScroll = 0;
		}

	if (mouseViewPane || mouseBotOrientationYZPane ||  mouseBotOrientationXYPane || mouseBotYZPane || mouseBotXZPane) {
		lastMouseX = x;
		lastMouseY = y;
		postRedisplay();
	}
}


void SubWindows3DMouseCallback(int button, int button_state, int x, int y )
{
	mouseViewPane = false;
	mouseBotXZPane = false;
	mouseBotYZPane = false;
	mouseBotOrientationXYPane = false;
	mouseBotOrientationYZPane = false;

	bool withShift = glutGetModifiers() & GLUT_ACTIVE_SHIFT;
	bool withCtrl= glutGetModifiers() & GLUT_ACTIVE_CTRL;

	if ( button == GLUT_LEFT_BUTTON && button_state == GLUT_DOWN && !withShift && !withCtrl) {
	    mouseViewPane = true;
	} else
	if ( button == GLUT_LEFT_BUTTON && button_state == GLUT_DOWN && withShift && !withCtrl) {
	    mouseBotXZPane = true;
	}else
	if ( button == GLUT_RIGHT_BUTTON && button_state == GLUT_DOWN && withShift && !withCtrl) {
	    mouseBotYZPane = true;
	}else
	if ( button == GLUT_LEFT_BUTTON && button_state == GLUT_DOWN && !withShift && withCtrl) {
	    mouseBotOrientationXYPane = true;
	} else
	if ( button == GLUT_RIGHT_BUTTON && button_state == GLUT_DOWN && !withShift && withCtrl) {
	    mouseBotOrientationYZPane = true;
	} else
	// Wheel reports as button 3(scroll up) and button 4(scroll down)
	if ((button == 3) || (button == 4)) // It's a wheel event
	{
		// Each wheel event reports like a button click, GLUT_DOWN then GLUT_UP
		if (button_state != GLUT_UP) { // Disregard redundant GLUT_UP events
			if (button == 3)
				lastMouseScroll++;
			else
				lastMouseScroll--;
			SubWindow3dMotionCallback(x,y); // scroll wheel does not trigger a glut call of MotionCallback, so we do it manually
		}
	}

	if (mouseViewPane || mouseBotOrientationYZPane ||  mouseBotOrientationXYPane || mouseBotYZPane || mouseBotXZPane) {
	    lastMouseX = x;
	    lastMouseY = y;
	}
}


void GluiReshapeCallback( int x, int y )
{
	reshape(x,y);
	int tx, ty, tw, th;
	int saveWindow = glutGetWindow();
	glutSetWindow(wMain);
	GLUI_Master.get_viewport_area( &tx, &ty, &tw, &th );
	glViewport( tx, ty, tw, th );
	glutSetWindow(saveWindow);
	postRedisplay();
}

// Idle Call back is used to check, whether anything has changed the
// bots position or view and it needs to be redrawn
void idleCallback( void )
{
	const int displayDelay = 300; 		// call display every 200ms at least (just in case)
	const int kinematicChangeDelay = 1000/50;// try to run with 50 fps

	static int idleCallbackCounter = 0;
	idleCallbackCounter = (idleCallbackCounter+1) % (displayDelay/kinematicChangeDelay);

	if (kinematicsHasChanged) {
		postRedisplay();
		kinematicsHasChanged = false;
	}
	else {
		if (idleCallbackCounter == 0)
			postRedisplay();
		else {
			TrajectoryView::getInstance().loop();
			delay(kinematicChangeDelay); // otherwise we needed 100% cpu, since idle callback is called in an infinite loop by glut
		}
	}

}

void layoutReset(int buttonNo) {
	JointAngles defaultPosition;
	defaultPosition.setDefaultPosition();
	for (int i = 0;i<NumberOfActuators;i++) {
		angleSpinner[i]->set_float_val(degrees(defaultPosition[i]));
	}
	angleSpinner[GRIPPER]->set_float_val(degrees(defaultPosition[GRIPPER]));

	// since angles have changed recompute kinematics. Call callback
	WindowController::getInstance().changedAnglesCallback();
}

void angleSpinnerCallback( int angleControlNumber )
{
	lastActiveSpinner = angleSpinner[angleControlNumber];

	// spinner values are changed with live variables. Round it
	static float lastSpinnerVal[NumberOfActuators];
	float lastValue = lastSpinnerVal[angleControlNumber];
	float value = anglesLiveVar[angleControlNumber];
	float roundedValue = roundValue(value);
	bool isIntType = angleSpinnerINT[angleControlNumber];
	if (isIntType)
		roundedValue = sgn(value)*((int)(abs(value)+0.5));
	if ((roundedValue == lastValue) && (roundedValue != value)) {
		if (isIntType)
			roundedValue += sgn(value-lastValue);
		else
			roundedValue += sgn(value-lastValue)*0.1;
	}
	lastSpinnerVal[angleControlNumber] = roundedValue;
	if (lastValue != roundedValue) {
		angleSpinner[angleControlNumber]->set_float_val(roundedValue);
		lastSpinnerVal[angleControlNumber] = roundedValue;
	}

	// since angles have changed recompute kinematics. Call callback
	WindowController::getInstance().changedAnglesCallback();
}

void poseSpinnerCallback( int tcpCoordId )
{
	// spinner values are changed with live variables
	static float lastSpinnerValue[7] = {0,0,0,0,0,0,0};

	// get value from live var and round it
	float lastValue = lastSpinnerValue[tcpCoordId];
	float value =poseSpinnerLiveVar[tcpCoordId];
	float roundedValue = roundValue(value);
	bool isIntType = poseSpinnerINT[tcpCoordId];
	if (isIntType)
		roundedValue = sgn(value)*((int)(abs(value)+0.5));

	if ((roundedValue == lastValue) && (roundedValue != value)) {
		if (isIntType)
			roundedValue += sgn(value-lastValue);
		else
			roundedValue += sgn(value-lastValue)*0.1;
	}
	if (lastValue != roundedValue) {
		poseSpinner[tcpCoordId ]->set_float_val(roundedValue);
		lastSpinnerValue[tcpCoordId] = roundedValue;
	}

	// compute angles out of tcp pose
	WindowController::getInstance().changedPoseCallback();
}

void handviewSpinnerCallback( int tcpCoordId )
{
	// spinner values are changed with live variables
	static float lastSpinnerValue[3] = {0,0,0};

	// get value from live var and round it
	float lastValue = lastSpinnerValue[tcpCoordId];
	float value =handviewSpinnerLiveVar[tcpCoordId];
	float roundedValue = roundValue(value);
	if ((roundedValue == lastValue) && (roundedValue != value)) {
		roundedValue += sgn(value-lastValue)*0.1;
	}
	if (lastValue != roundedValue) {
		handviewSpinner[tcpCoordId ]->set_float_val(roundedValue);
		lastSpinnerValue[tcpCoordId] = roundedValue;
	}

	// tell kinematics the new deviation from TCP
	Kinematics::getInstance().setTCPCoordinates(Point(handviewSpinnerLiveVar[X],handviewSpinnerLiveVar[Y],handviewSpinnerLiveVar[Z]));

	// compute angles out of tcp pose
	WindowController::getInstance().changedPoseCallback();
}

void handviewReset(int controlNo) {
	handviewSpinner[X]->set_float_val(0);
	handviewSpinner[Y]->set_float_val(0);
	handviewSpinner[Z]->set_float_val(0);

	// tell Kinematics to set new handview
	Kinematics::getInstance().setTCPCoordinates(Point(handviewSpinnerLiveVar[X],handviewSpinnerLiveVar[Y],handviewSpinnerLiveVar[Z]));

	// tell controller to re-compute the pose and display new pose
	WindowController::getInstance().changedPoseCallback();
}


void configurationViewCallback(int ControlNo) {
	PoseConfigurationType config = TrajectorySimulation::getInstance().getCurrentConfiguration();
	switch (ControlNo) {
	case ConfigurationTypeDirectionID:
		config.poseDirection = (PoseConfigurationType::PoseDirectionType)(1-config.poseDirection);
		break;
	case ConfigurationTypeFlipID:
		config.poseFlip = (PoseConfigurationType::PoseFlipType)(1-config.poseFlip);
		break;
	case ConfigurationViewTurnID:
		config.poseTurn = (PoseConfigurationType::PoseForearmType)(1-config.poseTurn);
		break;
	default:
		LOG(ERROR) << "configuration invalid";
	}

	const std::vector<KinematicsSolutionType>& solutions = TrajectorySimulation::getInstance().getPossibleSolutions();
	int changeConfigurationTries = 0;
	int changeConfigurationControl = ControlNo;
	do {
		changeConfigurationTries++;
		for (unsigned int i = 0;i<solutions.size();i++) {
			KinematicsSolutionType sol = solutions[i];
			if ((sol.config.poseDirection == config.poseDirection) &&
				(sol.config.poseFlip == config.poseFlip) &&
				(sol.config.poseTurn == config.poseTurn))
			{
				// key in angles manually and initiate forward kinematics
				for (unsigned int i = 0;i<NumberOfActuators;i++) {
					float value = degrees(sol.angles[i]);
					float roundedValue = roundValue(value);
					bool isIntType = angleSpinnerINT[i];
					if (isIntType)
						roundedValue = sgn(value)*((int)(abs(value)+0.5));
					angleSpinner[i]->set_float_val(roundedValue);
				}
				WindowController::getInstance().changedAnglesCallback();
				return; // solution is found, quit
			}
		}

		// we did not found a valid solution, we need to change another parameter to get a valid one
		changeConfigurationControl = (changeConfigurationControl+1) % 3;
		if (changeConfigurationControl == ControlNo)
			changeConfigurationControl = (changeConfigurationControl+1) % 3;

		switch (changeConfigurationControl) {
		case ConfigurationTypeDirectionID:
			config.poseDirection = (PoseConfigurationType::PoseDirectionType)(1-config.poseDirection);
			break;
		case ConfigurationTypeFlipID:
			config.poseFlip = (PoseConfigurationType::PoseFlipType)(1-config.poseFlip);
			break;
		case ConfigurationViewTurnID:
			config.poseTurn = (PoseConfigurationType::PoseForearmType)(1-config.poseTurn);
			break;
		default:
			LOG(ERROR) << "configuration invalid";
		}

	} while (changeConfigurationTries <= 3); // we have three configuration dimensions, so try two other ones max
	LOG(ERROR) << "valid configuration not found";
}

void WindowController::notifyNewBotData() {
	kinematicsHasChanged = true;
}

void WindowController::changedPoseCallback() {
	if (tcpCallback != NULL) {
		Pose newPose = getPoseView();
		(*tcpCallback)(newPose);
	}

	notifyNewBotData();
}

void WindowController::changedAnglesCallback() {
	if (anglesCallback != NULL) {
		JointAngles angles =getAnglesView();
		(*anglesCallback)(angles);
	}

	notifyNewBotData();
}


GLUI* WindowController::createInteractiveWindow(int mainWindow) {

	string emptyLine = "                                               ";

	GLUI *windowHandle= GLUI_Master.create_glui_subwindow( wMain,  GLUI_SUBWINDOW_RIGHT );
	windowHandle->set_main_gfx_window( wMain );

	GLUI_Panel* interactivePanel = new GLUI_Panel(windowHandle,"interactive panel", GLUI_PANEL_NONE);
	GLUI_Panel* kinematicsPanel = new GLUI_Panel(interactivePanel,"kinematics panel", GLUI_PANEL_NONE);
	kinematicsPanel->set_alignment(GLUI_ALIGN_RIGHT);

	GLUI_StaticText* text = new GLUI_StaticText(kinematicsPanel,"Actuator Angles");
    GLUI_Panel* AnglesPanel= new GLUI_Panel(kinematicsPanel,"angles panel", GLUI_PANEL_RAISED);
	text->set_alignment(GLUI_ALIGN_CENTER);
	string angleName[] = { "Hip","Upperarm","Forearm","Elbow", "Wrist", "Hand", "Gripper" };
	for (int i = 0;i<7;i++) {
		angleSpinner[i] = new GLUI_Spinner(AnglesPanel,angleName[i].c_str(), GLUI_SPINNER_FLOAT,&(anglesLiveVar[i]),i, angleSpinnerCallback);
		angleSpinner[i]->set_float_limits(degrees(actuatorConfigType[i].minAngle),degrees(actuatorConfigType[i].maxAngle));
		angleSpinner[i]->set_float_val(0.0);
	}

	windowHandle->add_column_to_panel(kinematicsPanel, false);
	text = new GLUI_StaticText(kinematicsPanel,"Inverse Kinematics");
	text->set_alignment(GLUI_ALIGN_CENTER);

	GLUI_Panel* TCPPanel= new GLUI_Panel(kinematicsPanel,"IK Panel", GLUI_PANEL_RAISED);

	string coordName[7] = {"x","y","z","Roll","Nick","Yaw", "Gripper" };
	for (int i = 0;i<7;i++) {
		poseSpinner[i]= new GLUI_Spinner(TCPPanel,coordName[i].c_str(), GLUI_SPINNER_FLOAT,&poseSpinnerLiveVar[i],i, poseSpinnerCallback);
	}
	poseSpinner[X]->set_float_limits(-1000,1000);
	poseSpinner[Y]->set_float_limits(-1000,1000);
	poseSpinner[Z]->set_float_limits(0,1000);

	poseSpinner[3]->set_float_limits(-360, 360);
	poseSpinner[4]->set_float_limits(-360, 360);
	poseSpinner[5]->set_float_limits(-360, 360);
	poseSpinner[6]->set_float_limits(0,
			(int)Kinematics::getInstance().getGripperDistance(actuatorConfigType[GRIPPER].maxAngle));

	windowHandle->add_column_to_panel(kinematicsPanel, false);
	text = new GLUI_StaticText(kinematicsPanel,"Tool Centre Point");
	text->set_alignment(GLUI_ALIGN_CENTER);

	GLUI_Panel* handviewPanel= new GLUI_Panel(kinematicsPanel,"Handview Panel", GLUI_PANEL_RAISED);

	for (int i = 0;i<3;i++) {
		handviewSpinner[i]= new GLUI_Spinner(handviewPanel,coordName[i].c_str(), GLUI_SPINNER_FLOAT,&handviewSpinnerLiveVar[i],i, handviewSpinnerCallback);
		handviewSpinner[i]->set_float_limits(-200,200);
	}

	new GLUI_StaticText(kinematicsPanel,"");
	GLUI_Panel* configurationPanel= new GLUI_Panel(kinematicsPanel,"configuration", GLUI_PANEL_NONE);
	new GLUI_StaticText(configurationPanel,"Configuration");

	confDirectionCheckbox = new GLUI_Checkbox( configurationPanel,"Dir ",&configDirectionLiveVar, ConfigurationTypeDirectionID, configurationViewCallback);
	confgFlipCheckbox = 	new GLUI_Checkbox( configurationPanel, "Flip", &configFlipLiveVar , ConfigurationTypeFlipID, configurationViewCallback);
	configTurnCheckbox = 	new GLUI_Checkbox( configurationPanel, "Turn",&configTurnLiveVar ,ConfigurationViewTurnID, configurationViewCallback);

	windowHandle->add_column_to_panel(configurationPanel, false);

	GLUI_Button* resetTcpButton = new GLUI_Button(configurationPanel, "Null TCP", 0, handviewReset);
	resetTcpButton->set_w(65);

	GLUI_Button* resetBotButton = 	new GLUI_Button(configurationPanel, "Null Bot", 0, layoutReset);
	resetBotButton->set_w(65);
	trajectoryView.create(windowHandle, interactivePanel);

	return windowHandle;
}


bool WindowController::setup(int argc, char** argv) {
	glutInit(&argc, argv);

	// start the initialization in a thread so that this function returns
	// (the thread runs the endless GLUT main loop)
	// So, the main thread can do something different while the UI is running
	eventLoopThread = new std::thread(&WindowController::UIeventLoop, this);

	// wait until UI is ready (excluding the startup animation)
	unsigned long startTime  = millis();
	do { delay(10); }
	while ((millis() - startTime < 20000) && (!uiReady));

	return uiReady;
}


void WindowController::UIeventLoop() {
	LOG(DEBUG) << "BotWindowCtrl::UIeventLoop";
	glutInitWindowSize(WindowWidth, WindowHeight);
    wMain = glutCreateWindow("Walter"); // Create a window with the given title
	glutInitWindowPosition(20, 20); // Position the window's initial top-left corner
	glutDisplayFunc(display);
	glutReshapeFunc(reshape);

	GLUI_Master.set_glutReshapeFunc( GluiReshapeCallback );
	GLUI_Master.set_glutIdleFunc( idleCallback);

	wMainBotView= mainBotView.create(wMain,"", BotView::_3D_VIEW, true);
	glutDisplayFunc(display);

	// Main view has comprehensive mouse motion
	glutSetWindow(wMainBotView);
	glutMotionFunc( SubWindow3dMotionCallback);
	glutMouseFunc( SubWindows3DMouseCallback);

	createInteractiveWindow(wMain);
	glutTimerFunc(0, StartupTimerCallback, 0);	// timer that sets the view point of startup procedure

	// double buffering
	glutInitDisplayMode(GLUT_DOUBLE);

	// set initial values of robot angles and position
	copyAnglesToView();
	copyConfigurationToView();
	copyPoseToView();

	uiReady = true; 							// tell calling thread to stop waiting for ui initialization
	LOG(DEBUG) << "starting GLUT main loop";
	glutMainLoop();  							// Enter the infinitely event-processing loop
}

// set callback invoked whenever an angle is changed via ui
void WindowController::setAnglesCallback(void (* callback)( const JointAngles& angles)) {
	anglesCallback = callback;
}

// set callback invoked whenever the tcp or configuration is changed via ui
void WindowController::setTcpInputCallback(bool (* callback)( const Pose& pose)) {
	tcpCallback = callback;
}

