#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include "TUM_Draw.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Ball.h"
#include "TUM_Utils.h"

#define mainGENERIC_PRIORITY	( tskIDLE_PRIORITY )
#define mainGENERIC_STACK_SIZE  ( ( unsigned short ) 2560 )

#define STATE_QUEUE_LENGTH 1

#define STATE_COUNT 2

#define STATE_ONE   0
#define STATE_TWO   1

#define NEXT_TASK   0 
#define PREV_TASK   1

#define STARTING_STATE  STATE_TWO

#define STATE_DEBOUNCE_DELAY   100 

#define PADDLE_Y_INCREMENT 5
#define PADDLE_LENGTH   SCREEN_HEIGHT / 5
#define PADDLE_Y_INCREMENTS (SCREEN_HEIGHT - PADDLE_LENGTH) / PADDLE_Y_INCREMENT
#define PADDLE_START_LOCATION_Y   SCREEN_HEIGHT / 2 - PADDLE_LENGTH / 2
#define PADDLE_EDGE_OFFSET  10
#define PADDLE_WIDTH    10

#define START_LEFT  1
#define START_RIGHT 0

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

static TaskHandle_t DemoTask1 = NULL;
static TaskHandle_t DemoTask2 = NULL;
static QueueHandle_t StateQueue = NULL;
static SemaphoreHandle_t DrawReady = NULL;

typedef struct buttons_buffer {
	unsigned char buttons[SDL_NUM_SCANCODES];
	SemaphoreHandle_t lock;
} buttons_buffer_t;

static buttons_buffer_t buttons = { 0 };

void checkDraw(unsigned char status, const char *msg) {
    if(status){
        if(msg)
            fprintf(stderr, "[ERROR] %s, %s\n", msg, tumGetErrorMessage());
        else
            fprintf(stderr, "[ERROR] %s\n", tumGetErrorMessage());

        exit(EXIT_FAILURE);
    }
}

/*
 * Changes the state, either forwards of backwards
 */
void changeState(volatile unsigned char *state, unsigned char forwards) {

	switch (forwards) {
	case NEXT_TASK:
		if (*state == STATE_COUNT - 1)
			*state = 0;
		else
			(*state)++;
		break;
	case PREV_TASK:
		if (*state == 0)
			*state = STATE_COUNT - 1;
		else
			(*state)--;
		break;
	default:
		break;
	}
}

/*
 * Example basic state machine with sequential states
 */
void basicSequentialStateMachine(void *pvParameters) {
	unsigned char current_state = STARTING_STATE; // Default state
	unsigned char state_changed = 1; // Only re-evaluate state if it has changed
	unsigned char input = 0;

	const int state_change_period = STATE_DEBOUNCE_DELAY;

	TickType_t last_change = xTaskGetTickCount();

    while (1) {
        if (state_changed)
            goto initial_state;

		// Handle state machine input
        if (xQueueReceive(StateQueue, &input, portMAX_DELAY) == pdTRUE) 
            if (xTaskGetTickCount() - last_change > state_change_period) {
                changeState(&current_state, input);
                state_changed = 1;
                last_change = xTaskGetTickCount();
            }

        initial_state:
        // Handle current state
        if (state_changed) {
            switch (current_state) {
            case STATE_ONE:
                vTaskSuspend(DemoTask2);
                vTaskResume(DemoTask1);
                break;
            case STATE_TWO:
                vTaskSuspend(DemoTask1);
                vTaskResume(DemoTask2);
                break;
            default:
                break;
            }
            state_changed = 0;
        }
    }
}

void vSwapBuffers(void *pvParameters) {
	TickType_t xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();
	const TickType_t frameratePeriod = 20;

	while (1) {
		xSemaphoreTake(DisplayReady, portMAX_DELAY);
		xSemaphoreGive(DrawReady);
		vDrawUpdateScreen();
		vTaskDelayUntil(&xLastWakeTime, frameratePeriod);
	}
}

#define KEYCODE(CHAR)       SDL_SCANCODE_##CHAR

void xGetButtonInput(void) {
	xSemaphoreTake(buttons.lock, portMAX_DELAY);
	xQueueReceive(inputQueue, &buttons.buttons, 0);
	xSemaphoreGive(buttons.lock);
}

#define CAVE_SIZE_X 	SCREEN_WIDTH / 2
#define CAVE_SIZE_Y 	SCREEN_HEIGHT / 2
#define CAVE_X 		    CAVE_SIZE_X / 2
#define CAVE_Y 		    CAVE_SIZE_Y / 2
#define CAVE_THICKNESS 	25

void vDrawCaveBoundingBox(void) {
	checkDraw(tumDrawFilledBox(CAVE_X - CAVE_THICKNESS, CAVE_Y - CAVE_THICKNESS,
		CAVE_SIZE_X + CAVE_THICKNESS * 2, CAVE_SIZE_Y + CAVE_THICKNESS * 2, Red),
        __FUNCTION__);
	
	checkDraw(tumDrawFilledBox(CAVE_X, CAVE_Y, CAVE_SIZE_X, CAVE_SIZE_Y, Aqua),
            __FUNCTION__);
}

void vDrawCave(void) {
	static unsigned short circlePositionX, circlePositionY;

    vDrawCaveBoundingBox();

	circlePositionX = CAVE_X + xGetMouseX() / 2;
	circlePositionY = CAVE_Y + xGetMouseY() / 2;

    tumDrawCircle(circlePositionX, circlePositionY, 20, Green);
}

void vDrawHelpText(void) {
	static char str[100] = { 0 };
	static unsigned int text_width;

	tumGetTextSize((char *) str, &text_width, NULL);

	sprintf(str, "[Q]uit, [C]hang[e] State");

    checkDraw(tumDrawText(str, SCREEN_WIDTH - text_width - 10, DEFAULT_FONT_SIZE * 0.5,
			Black), __FUNCTION__);
}

#define LOGO_FILENAME       "../resources/freertos.jpg"

void vDrawLogo(void) {
    static unsigned int image_height;
    tumGetImageSize(LOGO_FILENAME, NULL, &image_height);
    checkDraw(tumDrawScaledImage(LOGO_FILENAME, 10, 
                SCREEN_HEIGHT - 10 - image_height * 0.3, 0.3), __FUNCTION__);
}

void vDrawStaticItems(void) {
    vDrawHelpText();
    vDrawLogo();
}

void vDrawButtonText(void) {
	static char str[100] = { 0 };
	
    sprintf(str, "Axis 1: %5d | Axis 2: %5d", xGetMouseX(), xGetMouseY());

	checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 0.5, Black), __FUNCTION__);

	if (xSemaphoreTake(buttons.lock, portMAX_DELAY) == pdTRUE){
        sprintf(str, "W: %d | S: %d | A: %d | D: %d", buttons.buttons[KEYCODE(W)],
                buttons.buttons[KEYCODE(S)], buttons.buttons[KEYCODE(A)],
                buttons.buttons[KEYCODE(D)]);
        xSemaphoreGive(buttons.lock);
        checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 2, Black), __FUNCTION__);
    }

	if (xSemaphoreTake(buttons.lock, portMAX_DELAY) == pdTRUE) {
        sprintf(str, "UP: %d | DOWN: %d | LEFT: %d | RIGHT: %d",
                buttons.buttons[KEYCODE(UP)], buttons.buttons[KEYCODE(DOWN)],
                buttons.buttons[KEYCODE(LEFT)], buttons.buttons[KEYCODE(RIGHT)]);
        xSemaphoreGive(buttons.lock);
	    checkDraw(tumDrawText(str, 10, DEFAULT_FONT_SIZE * 3.5, Black), __FUNCTION__);
    }
}

void vCheckStateInput(void) {
	xGetButtonInput(); //Update global button data

	xSemaphoreTake(buttons.lock, portMAX_DELAY);
	if (buttons.buttons[KEYCODE(C)]) {
		xSemaphoreGive(buttons.lock);
		xQueueSend(StateQueue, &next_state_signal, portMAX_DELAY);
		return;
	}
	if (buttons.buttons[KEYCODE(E)]) {
		xSemaphoreGive(buttons.lock);
		xQueueSend(StateQueue, &prev_state_signal, portMAX_DELAY);
		return;
	}
	xSemaphoreGive(buttons.lock);
}

void vIncrementPaddleY(unsigned short *paddle) {
    if (*paddle != 0)
        (*paddle)--;
}

void vDecrementPaddleY(unsigned short *paddle) {
    if (*paddle != PADDLE_Y_INCREMENTS)
        (*paddle)++;
}

void vCheckPongInput(unsigned short *left_paddle_y, unsigned short *right_paddle_y) {
	xGetButtonInput(); //Update global button data

	xSemaphoreTake(buttons.lock, portMAX_DELAY);
	if (buttons.buttons[KEYCODE(C)]) {
		xSemaphoreGive(buttons.lock);
		xQueueSend(StateQueue, &next_state_signal, portMAX_DELAY);
		return;
	}
	if (buttons.buttons[KEYCODE(E)]) {
		xSemaphoreGive(buttons.lock);
		xQueueSend(StateQueue, &prev_state_signal, portMAX_DELAY);
		return;
	}
    if (buttons.buttons[KEYCODE(W)]) {
        vIncrementPaddleY(left_paddle_y);    
    }
    if (buttons.buttons[KEYCODE(S)]) {
        vDecrementPaddleY(left_paddle_y);
    }
    if (buttons.buttons[KEYCODE(UP)]) {
        vIncrementPaddleY(right_paddle_y);    
    }
    if (buttons.buttons[KEYCODE(DOWN)]) {
        vDecrementPaddleY(right_paddle_y);
    }
	xSemaphoreGive(buttons.lock);
}

void vDemoTask1(void *pvParameters) {
	signed char ret = 0;

	while (1) {
		if (xSemaphoreTake(DrawReady, portMAX_DELAY) == pdTRUE) {
            // Get input and check for state change
			vCheckStateInput();
            
            // Clear screen
            checkDraw(tumDrawClear(White), __FUNCTION__);
            
			vDrawCave();
            vDrawButtonText();
		}
	}
}

void playBallSound(void *args) {
    vPlaySample(a3);
}

typedef struct game_info{
    ball_t *ball;
    unsigned int left_score;
    unsigned int right_score;
    unsigned short left_paddle;
    unsigned short right_paddle;
}game_info_t;

void vResetPaddles(game_info_t *gi) {
    gi->left_paddle = PADDLE_Y_INCREMENTS / 2;
    gi->right_paddle = PADDLE_Y_INCREMENTS / 2;

}

void vRightWallCallback(void *gi) {
    //Reset ball's position and speed and increment left player's scrore
    setBallSpeed(((game_info_t *)gi)->ball, 0, 0, 0, SET_BALL_SPEED_AXES);
    ((game_info_t *)gi)->left_score++;
    vResetPaddles(gi);
    setBallLocation(((game_info_t *)gi)->ball, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
}

void vLeftWallCallback(void *gi) {
    setBallSpeed(((game_info_t *)gi)->ball, 0, 0, 0, SET_BALL_SPEED_AXES);
    ((game_info_t *)gi)->right_score++;
    vResetPaddles(gi);
}

void vDrawWall(wall_t *wall) {
    checkDraw(tumDrawFilledBox(wall->x1, wall->y1, wall->w, wall->h, 
                wall->colour), __FUNCTION__);
}

void vDrawPaddle(wall_t *wall, unsigned short y_increment) {
    // Set wall Y
    setWallProperty(wall, 0, y_increment * PADDLE_Y_INCREMENT + 1, 0, 0, SET_WALL_Y); 
    // Draw wall
    vDrawWall(wall);
}

#define SCORE_CENTER_OFFSET     20
#define SCORE_TOP_OFFSET        SCORE_CENTER_OFFSET

void vDrawScores(unsigned int left, unsigned int right) {
    static char buffer[5];
    static unsigned int size;
    sprintf(buffer, "%d", left);
    tumGetTextSize(buffer, &size, NULL);
    checkDraw(tumDrawText(buffer, SCREEN_WIDTH / 2 - size - SCORE_CENTER_OFFSET,
                SCORE_TOP_OFFSET, White), __FUNCTION__);
    sprintf(buffer, "%d", right);
    checkDraw(tumDrawText(buffer, SCREEN_WIDTH / 2 + SCORE_CENTER_OFFSET,
                SCORE_TOP_OFFSET, White), __FUNCTION__);
}

void vDemoTask2(void *pvParameters) {
	TickType_t xLastWakeTime, prevWakeTime;
	xLastWakeTime = xTaskGetTickCount();
    prevWakeTime = xLastWakeTime;
	const TickType_t updatePeriod = 10;
    
    ball_t *my_ball = createBall(SCREEN_WIDTH / 2, SCREEN_HEIGHT/2, White, 20,
            1000, &playBallSound, NULL);

    game_info_t game_info = {0};
    game_info.ball = my_ball;
    unsigned int left_score = 0;
    unsigned int right_score = 0;
    unsigned char ball_moving = 0;
    unsigned char ball_start_direction = 0;
    vResetPaddles(&game_info);

    setBallSpeed(my_ball, 250, 250, 0, SET_BALL_SPEED_AXES);

    //Left wall
    wall_t *left_wall = createWall(1, 1, 1, SCREEN_HEIGHT, 0.1, White, 
            &vLeftWallCallback, &game_info);
    //Right wall
    wall_t *right_wall = createWall(SCREEN_WIDTH - 1, 1, 1, SCREEN_HEIGHT, 0.1, 
            White, &vRightWallCallback, &game_info);
    //Top wall
    wall_t *top_wall = createWall(1, 1, SCREEN_WIDTH, 1, 0.1, White, NULL, NULL);
    //Bottom wall
    wall_t *bottom_wall = createWall(1, SCREEN_HEIGHT - 1, SCREEN_WIDTH, 1, 
            0.1, White, NULL, NULL);

    //Left paddle
    wall_t *left_paddle = createWall (PADDLE_EDGE_OFFSET, PADDLE_START_LOCATION_Y, 
            PADDLE_WIDTH, PADDLE_LENGTH, 0.1, White, NULL, NULL);

    //Right paddle
    wall_t *right_paddle = createWall(SCREEN_WIDTH - PADDLE_EDGE_OFFSET - PADDLE_WIDTH,
            PADDLE_START_LOCATION_Y, PADDLE_WIDTH, PADDLE_LENGTH, 0.1, White, 
            NULL, NULL);

    while(1) {
		if (xSemaphoreTake(DrawReady, portMAX_DELAY) == pdTRUE) {
            // Get input 
			vCheckPongInput(&game_info.left_paddle, &game_info.right_paddle);
            
            // Clear screen
		    checkDraw(tumDrawClear(Black), __FUNCTION__);
            
            // Draw the walls
            vDrawWall(left_wall);
            vDrawWall(right_wall);
            vDrawWall(top_wall);
            vDrawWall(bottom_wall);

            // Draw the paddles
            vDrawPaddle(left_paddle, game_info.left_paddle);
            vDrawPaddle(right_paddle, game_info.right_paddle);

            vDrawScores(game_info.left_score, game_info.right_score);

            // Check if ball has made a collision
            checkBallCollisions(my_ball, NULL, NULL);
        
            // Update the balls position now that possible collisions have
            // updated its speeds
            updateBallPosition(my_ball, xLastWakeTime - prevWakeTime);

            // Draw the ball
            checkDraw(tumDrawCircle(my_ball->x, my_ball->y, my_ball->radius, 
                        my_ball->colour), __FUNCTION__);

            //Keep track of when task last ran so that you know how many ticks
            //(in our case miliseconds) have passed so that the balls position
            //can be updated appropriatley
            prevWakeTime = xLastWakeTime;
		    vTaskDelayUntil(&xLastWakeTime, updatePeriod);
        }
    }
}

int main(int argc, char *argv[]) {

    char *bin_folder_path = getBinFolderPath(argv[0]);

	vInitDrawing(bin_folder_path);
	vInitEvents();
    vInitAudio(bin_folder_path);

	xTaskCreate(vDemoTask1, "DemoTask1", mainGENERIC_STACK_SIZE, NULL,
	    mainGENERIC_PRIORITY, &DemoTask1);
	xTaskCreate(vDemoTask2, "DemoTask2", mainGENERIC_STACK_SIZE, NULL,
	    mainGENERIC_PRIORITY, &DemoTask2);
	xTaskCreate(basicSequentialStateMachine, "StateMachine",
	    mainGENERIC_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, NULL);
	xTaskCreate(vSwapBuffers, "BufferSwapTask", mainGENERIC_STACK_SIZE, NULL,
	    configMAX_PRIORITIES, NULL);

	vTaskSuspend(DemoTask1);
	vTaskSuspend(DemoTask2);

	buttons.lock = xSemaphoreCreateMutex(); //Locking mechanism

	if (!buttons.lock) {
		printf("Button lock mutex not created\n");
		exit(EXIT_FAILURE);
	}

	DrawReady = xSemaphoreCreateBinary(); //Sync signal

	if (!DrawReady) {
		printf("DrawReady semaphore not created\n");
		exit(EXIT_FAILURE);
	}

	//Message sending
	StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));

	if (!StateQueue) {
		printf("StateQueue queue not created\n");
		exit(EXIT_FAILURE);
	}

	vTaskStartScheduler();

	return EXIT_SUCCESS;
}

void vMainQueueSendPassed(void) {
	/* This is just an example implementation of the "queue send" trace hook. */
}

void vApplicationIdleHook(void) {
#ifdef __GCC_POSIX__
	struct timespec xTimeToSleep, xTimeSlept;
	/* Makes the process more agreeable when using the Posix simulator. */
	xTimeToSleep.tv_sec = 1;
	xTimeToSleep.tv_nsec = 0;
	nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
