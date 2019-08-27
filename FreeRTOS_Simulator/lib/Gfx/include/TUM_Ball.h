#ifndef __TUM_BALL_H__
#define __TUM_BALL_H__

/**
 * @brief
 *
 *
 */
typedef struct ball{
    unsigned short x;       /**< X pixel co-ord of ball on screen */
    unsigned short y;       /**< Y pixel co-ord of ball on screen */

    float f_x;              /**< Absolute X location of ball */
    float f_y;              /**< Absolute Y location of ball */

    float dx;               /**< X axis speed in pixels/second */
    float dy;               /**< Y axis speed in pixels/second */

    float max_speed;        /**< Maximum speed the ball is able to achieve in
                            pixels/second */

    unsigned int colour;    /**< Hex RGB colour of the ball */

    unsigned short radius;  /**< Radius the the ball in pixels */

    void (*callback)(void); /**< Collision callback */
}ball_t;

/**
 * @brief Object to represent a wall that balls bounce off of
 *
 * A wall object is created by passing the top left X and Y locations (in pixels)
 * and the width and height of the desired wall. The wall also stores a colour that
 * can be used to render it, allowing for the information to be stored in the object.
 * A wall interacts with balls automatically as all walls generated are stored in
 * a list that is itterated though by the function checkBallCollisions.
 *
 * When a wall is collided with it causes a ball to loose or gain speed, the
 * dampening is a normalized percentage value that is used to either increase or
 * decrease the balls velocity. A dampening of -0.4 represents a 40% decrease in
 * speed, similarly 0.4 represents a 40% increase in speed.
 *
 * Please be aware that the position of a ball can be tested slower than a ball
 * can move when the ball is moving extremly quickly, this can cause the balls to
 * jump over objects, this is due to the extremly simple collision detection
 * implemented.
 *
 * A walls callback is a function pointer taking a function of the format
 * void (*callback)(void). If the function is set the that function is called
 * when the wall is collided with. This allows for actions to be performed when
 * a specific wall is collided with.
 */
typedef struct wall{
    unsigned short x1;      /**< Top left corner X coord of wall */
    unsigned short y1;      /**< Top left corner Y coord of wall */

    unsigned short w;       /**< Width of wall (X axis) */
    unsigned short h;       /**< Height of wall (Y axis) */

    unsigned short x2;      /**< Bottom right corner X coord of wall */
    unsigned short y2;      /**< Bottom right corner Y coord of wall */

    float dampening;        /**< Value by which a balls speed is changed,
                            eg. 0.2 represents a 20% increase in speed*/
    
    unsigned int colour;    /**< Hex RGB colour of the ball */
    
    void (*callback)(void); /**< Collision callback */
}wall_t;

/**
 * @brief
 *
 *
 * @param
 * @return
 */
ball_t *createBall(unsigned short initial_x, unsigned short initial_y,
        unsigned int colour, unsigned short radius, float max_speed, 
        void (*callback)());
wall_t *createWall(unsigned short x1, unsigned short y1, unsigned short w,
        unsigned short h, float dampening, unsigned int colour, 
        void (*callback)());

void setBallSpeed(ball_t *ball, float dx, float dy);
void checkBallCollisions(ball_t *ball, void (*callback)());
void updateBallPosition(ball_t *ball, unsigned int mili_seconds);

#endif
