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

#define STARTING_STATE  STATE_ONE

#define STATE_DEBOUNCE_DELAY   100 

#define PADDLE_Y_INCREMENT 5
#define PADDLE_LENGTH   SCREEN_HEIGHT / 5
#define PADDLE_Y_INCREMENTS (SCREEN_HEIGHT - PADDLE_LENGTH) / PADDLE_Y_INCREMENT
#define PADDLE_START_LOCATION_Y   SCREEN_HEIGHT / 2 - PADDLE_LENGTH / 2
#define PADDLE_EDGE_OFFSET  10
#define PADDLE_WIDTH    10

#define START_LEFT  1
#define START_RIGHT 2

const unsigned char start_left = START_LEFT;
const unsigned char start_right = START_RIGHT;

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

static TaskHandle_t LeftPaddleTask = NULL;
static TaskHandle_t RightPaddleTask = NULL;
static TaskHandle_t PongControlTask = NULL;
static TaskHandle_t PausedStateTask = NULL;

static QueueHandle_t StateQueue = NULL;
static QueueHandle_t LeftScoreQueue = NULL;
static QueueHandle_t RightScoreQueue = NULL;
static QueueHandle_t StartDirectionQueue = NULL;

static SemaphoreHandle_t DrawReady = NULL;
static SemaphoreHandle_t BallInactive = NULL;

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
                vTaskSuspend(PausedStateTask);
                vTaskResume(PongControlTask);
                vTaskResume(LeftPaddleTask);
                vTaskResume(RightPaddleTask);
                break;
            case STATE_TWO: //paused
                vTaskSuspend(PongControlTask);
                vTaskSuspend(LeftPaddleTask);
                vTaskSuspend(RightPaddleTask);
                vTaskResume(PausedStateTask);
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

void vIncrementPaddleY(unsigned short *paddle) {
    if(paddle)
        if (*paddle != 0)
            (*paddle)--;
}

void vDecrementPaddleY(unsigned short *paddle) {
    if(paddle)
        if (*paddle != PADDLE_Y_INCREMENTS)
            (*paddle)++;
}

unsigned char xCheckPongRightInput(unsigned short *right_paddle_y) {
	xGetButtonInput(); //Update global button data

	xSemaphoreTake(buttons.lock, portMAX_DELAY);
    if (buttons.buttons[KEYCODE(UP)]) {
        vIncrementPaddleY(right_paddle_y);    
	    xSemaphoreGive(buttons.lock);
        return 1;
    }
    if (buttons.buttons[KEYCODE(DOWN)]) {
        vDecrementPaddleY(right_paddle_y);
        xSemaphoreGive(buttons.lock);
        return 1;
    }
	xSemaphoreGive(buttons.lock);
    return 0;
}

unsigned char xCheckPongLeftInput(unsigned short *left_paddle_y) {
	xGetButtonInput(); //Update global button data

	xSemaphoreTake(buttons.lock, portMAX_DELAY);
    if (buttons.buttons[KEYCODE(W)]) {
        vIncrementPaddleY(left_paddle_y);    
	    xSemaphoreGive(buttons.lock);
        return 1;
    }
    if (buttons.buttons[KEYCODE(S)]) {
        vDecrementPaddleY(left_paddle_y);
	    xSemaphoreGive(buttons.lock);
        return 1;
    }
	xSemaphoreGive(buttons.lock);
    return 0;
}

unsigned char xCheckForInput(void) {
    if(xCheckPongLeftInput(NULL) || xCheckPongRightInput(NULL))
        return 1;
    return 0;
}

void playBallSound(void *args) {
    vPlaySample(a3);
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
    sprintf(buffer, "%d", right);
    tumGetTextSize(buffer, &size, NULL);
    checkDraw(tumDrawText(buffer, SCREEN_WIDTH / 2 - size - SCORE_CENTER_OFFSET,
                SCORE_TOP_OFFSET, White), __FUNCTION__);
    sprintf(buffer, "%d", left);
    checkDraw(tumDrawText(buffer, SCREEN_WIDTH / 2 + SCORE_CENTER_OFFSET,
                SCORE_TOP_OFFSET, White), __FUNCTION__);
}

typedef struct player_data{
    wall_t *paddle;
    unsigned short paddle_position;
    unsigned int score;
}player_data_t;

void vResetPaddle(wall_t *wall){
   setWallProperty(wall, 0, PADDLE_Y_INCREMENTS / 2, 0, 0, SET_WALL_Y); 
}

void vRightWallCallback(void *player_data) {
    //Reset ball's position and speed and increment left player's score
    ((player_data_t *)player_data)->score++;
    if (RightScoreQueue)
        xQueueOverwrite(RightScoreQueue, &((player_data_t *)player_data)->score);
    vResetPaddle(((player_data_t *)player_data)->paddle);
    xSemaphoreGive(BallInactive);
    xQueueOverwrite(StartDirectionQueue, &start_right);
}

void vRightPaddleTask(void *pvParameters) {
    player_data_t right_player = {0};
    right_player.paddle_position = PADDLE_Y_INCREMENTS / 2;

    //Right wall
    wall_t *right_wall = createWall(SCREEN_WIDTH - 1, 1, 1, SCREEN_HEIGHT, 0.1, 
            White, &vRightWallCallback, &right_player);
    //Right paddle
    right_player.paddle = createWall(SCREEN_WIDTH - PADDLE_EDGE_OFFSET - PADDLE_WIDTH,
            PADDLE_START_LOCATION_Y, PADDLE_WIDTH, PADDLE_LENGTH, 0.1, White, 
            NULL, NULL);
    
    RightScoreQueue = xQueueCreate(1, sizeof(unsigned int));

	while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 

        // Get input 
        xCheckPongRightInput(&right_player.paddle_position);
        
        vDrawWall(right_wall);
        vDrawPaddle(right_player.paddle, right_player.paddle_position);
	}
}

void vLeftWallCallback(void *player_data) {
    ((player_data_t *)player_data)->score++;
    if (LeftScoreQueue)
        xQueueOverwrite(LeftScoreQueue, &((player_data_t *)player_data)->score);
    vResetPaddle(((player_data_t *)player_data)->paddle);
    xSemaphoreGive(BallInactive);
    xQueueOverwrite(StartDirectionQueue, &start_left);
}

void vLeftPaddleTask(void *pvParameters) {
    player_data_t left_player = {0};
    left_player.paddle_position = PADDLE_Y_INCREMENTS / 2;

    //Left wall
    wall_t *left_wall = createWall(1, 1, 1, SCREEN_HEIGHT, 0.1, White, 
            &vLeftWallCallback, &left_player);
    //Left paddle
    left_player.paddle = createWall (PADDLE_EDGE_OFFSET, PADDLE_START_LOCATION_Y, 
            PADDLE_WIDTH, PADDLE_LENGTH, 0.1, White, NULL, NULL);
    
    LeftScoreQueue = xQueueCreate(1, sizeof(unsigned int));
    
	while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); 
        
        // Get input 
        xCheckPongLeftInput(&left_player.paddle_position);

        vDrawWall(left_wall);
        vDrawPaddle(left_player.paddle, left_player.paddle_position);
	}
}

void vPongControlTask(void *pvParameters) {
	TickType_t xLastWakeTime, prevWakeTime;
	xLastWakeTime = xTaskGetTickCount();
    prevWakeTime = xLastWakeTime;
	const TickType_t updatePeriod = 10;
    
    ball_t *my_ball = createBall(SCREEN_WIDTH / 2, SCREEN_HEIGHT/2, White, 20,
            1000, &playBallSound, NULL);

    unsigned char ball_active = 0;
    unsigned char ball_direction = 0;
	
    unsigned int left_score = 0;
    unsigned int right_score = 0;

    BallInactive = xSemaphoreCreateBinary();
    StartDirectionQueue = xQueueCreate(1, sizeof(unsigned char));

    setBallSpeed(my_ball, 250, 250, 0, SET_BALL_SPEED_AXES);

    //Top wall
    wall_t *top_wall = createWall(1, 1, SCREEN_WIDTH, 1, 0.1, White, NULL, NULL);
    //Bottom wall
    wall_t *bottom_wall = createWall(1, SCREEN_HEIGHT - 1, SCREEN_WIDTH, 1, 
            0.1, White, NULL, NULL);

    while(1) {
		if (xSemaphoreTake(DrawReady, portMAX_DELAY) == pdTRUE) {
            xGetButtonInput(); //Update global button data

            xSemaphoreTake(buttons.lock, portMAX_DELAY);
            if (buttons.buttons[KEYCODE(P)]) {
                xSemaphoreGive(buttons.lock);
                xQueueSend(StateQueue, &next_state_signal, portMAX_DELAY);
            }
            
            if (buttons.buttons[KEYCODE(R)]) {
                ball_active = 0;
                setBallLocation(my_ball, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
                setBallSpeed(my_ball, 0, 0, 0, SET_BALL_SPEED_AXES);
                left_score = 0;
                right_score = 1;
            }
            xSemaphoreGive(buttons.lock);
            
            // Ball is no longer active
            if(xSemaphoreTake(BallInactive, 0) == pdTRUE) {
                ball_active = 0;
            }

            if(!ball_active){
                setBallLocation(my_ball, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
                setBallSpeed(my_ball, 0, 0, 0, SET_BALL_SPEED_AXES);
                
                // Draw the ball
                checkDraw(tumDrawCircle(my_ball->x, my_ball->y, my_ball->radius,
                            my_ball->colour), __FUNCTION__);

                if(xCheckForInput()){
                    xQueueReceive(StartDirectionQueue, &ball_direction, 0);
                    ball_active = 1;
                    switch(ball_direction){
                    case START_LEFT:
                        setBallSpeed(my_ball, -250, 250, 0, SET_BALL_SPEED_AXES);
                        break;
                    default:
                    case START_RIGHT:
                        setBallSpeed(my_ball, 250, 250, 0, SET_BALL_SPEED_AXES);
                        break;
                    }
                }
            }

            xTaskNotifyGive(LeftPaddleTask);
            xTaskNotifyGive(RightPaddleTask);
        
            checkDraw(tumDrawClear(Black), __FUNCTION__);

            // Draw the walls
            vDrawWall(top_wall);
            vDrawWall(bottom_wall);

            if (LeftScoreQueue)
                xQueueReceive(LeftScoreQueue, &left_score, 0);
            if (RightScoreQueue)
                xQueueReceive(RightScoreQueue, &right_score, 0);
            vDrawScores(left_score, right_score);

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

void vPausedStateTask (void *pvParameters) {
    while(1){
        xGetButtonInput(); //Update global button data

        xSemaphoreTake(buttons.lock, portMAX_DELAY);
        if (buttons.buttons[KEYCODE(P)]) {
            xSemaphoreGive(buttons.lock);
            xQueueSend(StateQueue, &next_state_signal, portMAX_DELAY);
        }
        xSemaphoreGive(buttons.lock);

        vTaskDelay(10);
    }
}

int main(int argc, char *argv[]) {

    char *bin_folder_path = getBinFolderPath(argv[0]);

	vInitDrawing(bin_folder_path);
	vInitEvents();
    vInitAudio(bin_folder_path);

	xTaskCreate(vLeftPaddleTask, "LeftPaddleTask", mainGENERIC_STACK_SIZE, NULL,
	    mainGENERIC_PRIORITY, &LeftPaddleTask);
	xTaskCreate(vRightPaddleTask, "RightPaddleTask", mainGENERIC_STACK_SIZE, NULL,
	    mainGENERIC_PRIORITY, &RightPaddleTask);
	xTaskCreate(vPausedStateTask, "PausedStateTask", mainGENERIC_STACK_SIZE, NULL,
	    mainGENERIC_PRIORITY, &PausedStateTask);
	xTaskCreate(vPongControlTask, "PongControlTask", mainGENERIC_STACK_SIZE, NULL,
	    mainGENERIC_PRIORITY, &PongControlTask);
	xTaskCreate(basicSequentialStateMachine, "StateMachine",
	    mainGENERIC_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, NULL);
	xTaskCreate(vSwapBuffers, "BufferSwapTask", mainGENERIC_STACK_SIZE, NULL,
	    configMAX_PRIORITIES, NULL);

	vTaskSuspend(LeftPaddleTask);
	vTaskSuspend(RightPaddleTask);
	vTaskSuspend(PongControlTask);
    vTaskSuspend(PausedStateTask);

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
