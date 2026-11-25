/*******************************************************************************************
*

*
********************************************************************************************/

#include "raylib.h"
#include <math.h>

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

//----------------------------------------------------------------------------------
// Global Variables Definition (local to this module)
//----------------------------------------------------------------------------------
Camera camera = { 0 };
// The cube is centered at this position; y = 1 puts its bottom on the ground.
Vector3 cubePosition = { 0.0f, 1.0f, 0.0f };
float cubeVerticalVelocity = 0.0f;

// Camera orbit state: yaw turns around the target, and radius/height set its distance and elevation.
float cameraYaw = 0.0f;
float cameraOrbitRadius = 0.0f;
float cameraOrbitHeight = 0.0f;
float cameraElevation = 0.0f;

// Start, destination, and elapsed time for the animated Plan/3D view transition.
float cameraViewTransitionStart = 0.0f;
float cameraViewTransitionTarget = 0.0f;
float cameraViewTransitionTime = -1.0f;

// Start/destination camera distances and elapsed time for animated zooming.
float cameraZoomStartRadius = 0.0f;
float cameraZoomTargetRadius = 0.0f;
float cameraZoomStartHeight = 0.0f;
float cameraZoomTargetHeight = 0.0f;
float cameraZoomTime = -1.0f;

// Start/destination yaw and elapsed time for each animated 45-degree swivel.
float cameraTurnStartYaw = 0.0f;
float cameraTurnTargetYaw = 0.0f;
float cameraTurnTime = -1.0f;

// Arrow-key presses wait here while another swivel animation is running.
int pendingCameraTurns = 0;
bool cameraPlanView = false;

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
static void UpdateDrawFrame(void);          // Update and draw one frame
static void UpdateCameraPosition(void);

//----------------------------------------------------------------------------------
// Program main entry point
//----------------------------------------------------------------------------------
int main()
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "raylib");

    // Set the initial angled view, then derive the orbit values used by the camera animations.
    camera.position = (Vector3){ 10.0f, 10.0f, 8.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    cameraYaw = atan2f(camera.position.x - camera.target.x, camera.position.z - camera.target.z);
    cameraOrbitRadius = sqrtf(
        (camera.position.x - camera.target.x)*(camera.position.x - camera.target.x) +
        (camera.position.z - camera.target.z)*(camera.position.z - camera.target.z));
    cameraOrbitHeight = camera.position.y - camera.target.y;
    cameraElevation = atan2f(cameraOrbitHeight, cameraOrbitRadius);

    //--------------------------------------------------------------------------------------

#if defined(PLATFORM_WEB)
    // Emscripten repeatedly calls the frame function for browser builds.
    emscripten_set_main_loop(UpdateDrawFrame, 60, 1);
#else
    SetTargetFPS(60);               // Set game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        UpdateDrawFrame();
    }
#endif

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();                  // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}

// Update and draw game frame
static void UpdateDrawFrame(void)
{
    // Update
    //----------------------------------------------------------------------------------
    float frameTime = GetFrameTime();
    float moveX = 0.0f;
    float moveZ = 0.0f;

    // Read WASD as screen-relative movement, then normalize so diagonal movement is not faster
    if (IsKeyDown(KEY_A)) moveX -= 1.0f;
    if (IsKeyDown(KEY_D)) moveX += 1.0f;
    if (IsKeyDown(KEY_W)) moveZ -= 1.0f;
    if (IsKeyDown(KEY_S)) moveZ += 1.0f;

    float moveLength = sqrtf(moveX*moveX + moveZ*moveZ);
    if (moveLength > 0.0f)
    {
        const float moveSpeed = 5.0f;
        float screenRight = moveX/moveLength;
        float screenDown = moveZ/moveLength;
        cubePosition.x += (cosf(cameraYaw)*screenRight + sinf(cameraYaw)*screenDown)*moveSpeed*frameTime;
        cubePosition.z += (-sinf(cameraYaw)*screenRight + cosf(cameraYaw)*screenDown)*moveSpeed*frameTime;
    }

    // Smoothly move both camera position and target by the same amount to follow the cube on the ground.
    float followAmount = 1.0f - expf(-3.0f*frameTime);
    float cameraFollowX = (cubePosition.x - camera.target.x)*followAmount;
    float cameraFollowZ = (cubePosition.z - camera.target.z)*followAmount;
    camera.target.x += cameraFollowX;
    camera.target.z += cameraFollowZ;
    camera.position.x += cameraFollowX;
    camera.position.z += cameraFollowZ;

    // Apply a jump impulse when grounded, then gravity each frame and clamp the cube back to the floor.
    if (IsKeyPressed(KEY_SPACE) && cubePosition.y <= 1.0f)
        cubeVerticalVelocity = 8.0f;

    cubeVerticalVelocity -= 20.0f*frameTime;
    cubePosition.y += cubeVerticalVelocity*frameTime;
    if (cubePosition.y < 1.0f)
    {
        cubePosition.y = 1.0f;
        cubeVerticalVelocity = 0.0f;
    }

    // Place the zoom and view-mode buttons at the top-right, adapting to the current window width.
    const float zoomInButtonSize = 36.0f;
    const float zoomButtonGap = 8.0f;
    float zoomButtonsX = (float)GetScreenWidth() - 12.0f - 2.0f*zoomInButtonSize - zoomButtonGap;
    Rectangle zoomOutButton = { zoomButtonsX, 12.0f, zoomInButtonSize, zoomInButtonSize };
    Rectangle zoomInButton = { zoomButtonsX + zoomInButtonSize + zoomButtonGap, 12.0f,
                               zoomInButtonSize, zoomInButtonSize };
    Rectangle viewButton = { zoomButtonsX - zoomButtonGap - 72.0f, 12.0f, 72.0f, zoomInButtonSize };
    Vector2 mousePosition = GetMousePosition();

    // Clicking Plan/3D starts a view-angle animation; clicking +/- starts a bounded zoom animation.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        if (CheckCollisionPointRec(mousePosition, viewButton))
        {
            cameraPlanView = !cameraPlanView;
            cameraViewTransitionStart = cameraElevation;
            cameraViewTransitionTarget = cameraPlanView? 1.57079632679f :
                                         atan2f(cameraOrbitHeight, cameraOrbitRadius);
            cameraViewTransitionTime = 0.0f;
        }
        else
        {
            float zoomFactor = 1.0f;
            if (CheckCollisionPointRec(mousePosition, zoomInButton)) zoomFactor = 0.8f;
            else if (CheckCollisionPointRec(mousePosition, zoomOutButton)) zoomFactor = 1.25f;

            if (zoomFactor != 1.0f)
            {
                cameraZoomStartRadius = cameraOrbitRadius;
                cameraZoomStartHeight = cameraOrbitHeight;
                cameraZoomTargetRadius = cameraOrbitRadius*zoomFactor;
                cameraZoomTargetHeight = cameraOrbitHeight*zoomFactor;
                if (cameraZoomTargetRadius < 2.0f)
                {
                    cameraZoomTargetHeight *= 2.0f/cameraZoomTargetRadius;
                    cameraZoomTargetRadius = 2.0f;
                }
                else if (cameraZoomTargetRadius > 40.0f)
                {
                    cameraZoomTargetHeight *= 40.0f/cameraZoomTargetRadius;
                    cameraZoomTargetRadius = 40.0f;
                }
                cameraZoomTime = 0.0f;
            }
        }
    }

    // Queue one 45-degree camera turn per arrow-key press so fast presses are not lost
    if (IsKeyPressed(KEY_LEFT)) pendingCameraTurns--;
    if (IsKeyPressed(KEY_RIGHT)) pendingCameraTurns++;

    if (cameraTurnTime < 0.0f && pendingCameraTurns != 0)
    {
        int direction = (pendingCameraTurns < 0)? -1 : 1;
        pendingCameraTurns -= direction;
        cameraTurnStartYaw = cameraYaw;
        cameraTurnTargetYaw = cameraYaw + direction*0.78539816339f;
        cameraTurnTime = 0.0f;
    }

    if (cameraTurnTime >= 0.0f)
    {
        // Smoothstep eases the camera into and out of each swivel.
        const float turnDuration = 0.4f;
        cameraTurnTime += frameTime;
        float progress = cameraTurnTime/turnDuration;
        if (progress >= 1.0f)
        {
            progress = 1.0f;
            cameraTurnTime = -1.0f;
        }

        float easedProgress = progress*progress*(3.0f - 2.0f*progress);
        cameraYaw = cameraTurnStartYaw + (cameraTurnTargetYaw - cameraTurnStartYaw)*easedProgress;
        UpdateCameraPosition();
    }

    if (cameraViewTransitionTime >= 0.0f)
    {
        // Change elevation between top-down and the original angled view without changing orbit distance.
        const float transitionDuration = 0.4f;
        cameraViewTransitionTime += frameTime;
        float progress = cameraViewTransitionTime/transitionDuration;
        if (progress >= 1.0f)
        {
            progress = 1.0f;
            cameraViewTransitionTime = -1.0f;
        }

        float easedProgress = progress*progress*(3.0f - 2.0f*progress);
        cameraElevation = cameraViewTransitionStart +
                          (cameraViewTransitionTarget - cameraViewTransitionStart)*easedProgress;
        UpdateCameraPosition();
    }

    if (cameraZoomTime >= 0.0f)
    {
        // Interpolate the orbit dimensions to the selected zoom level using the same easing curve.
        const float zoomDuration = 0.4f;
        cameraZoomTime += frameTime;
        float progress = cameraZoomTime/zoomDuration;
        if (progress >= 1.0f)
        {
            progress = 1.0f;
            cameraZoomTime = -1.0f;
        }

        float easedProgress = progress*progress*(3.0f - 2.0f*progress);
        cameraOrbitRadius = cameraZoomStartRadius +
                            (cameraZoomTargetRadius - cameraZoomStartRadius)*easedProgress;
        cameraOrbitHeight = cameraZoomStartHeight +
                            (cameraZoomTargetHeight - cameraZoomStartHeight)*easedProgress;
        UpdateCameraPosition();
    }
    //----------------------------------------------------------------------------------

    // Draw
    //----------------------------------------------------------------------------------
    BeginDrawing();

        ClearBackground(RAYWHITE);

        // Draw the 3D scene using the camera position calculated during the update step.
        BeginMode3D(camera);

            DrawCube(cubePosition, 2.0f, 2.0f, 2.0f, RED);
            DrawCubeWires(cubePosition, 2.0f, 2.0f, 2.0f, MAROON);
            DrawLine3D((Vector3){ -5.0f, 0.01f, 0.0f }, (Vector3){ 5.0f, 0.01f, 0.0f }, ORANGE);
            DrawLine3D((Vector3){ 0.0f, 0.01f, -5.0f }, (Vector3){ 0.0f, 0.01f, 5.0f }, DARKBLUE);
            DrawGrid(10, 1.0f);

        EndMode3D();

        // Draw controls and instructions in 2D on top of the 3D scene
        DrawText("WASD: move relative to view | Space: jump | Left/Right: swivel 45 degrees", 10, 40, 20, DARKGRAY);

        DrawRectangleRec(zoomOutButton, CheckCollisionPointRec(mousePosition, zoomOutButton)? LIGHTGRAY : RAYWHITE);
        DrawRectangleLinesEx(zoomOutButton, 2.0f, DARKGRAY);
        DrawText("-", (int)zoomOutButton.x + 13, (int)zoomOutButton.y + 2, 30, DARKGRAY);
        DrawRectangleRec(zoomInButton, CheckCollisionPointRec(mousePosition, zoomInButton)? LIGHTGRAY : RAYWHITE);
        DrawRectangleLinesEx(zoomInButton, 2.0f, DARKGRAY);
        DrawText("+", (int)zoomInButton.x + 8, (int)zoomInButton.y + 2, 30, DARKGRAY);
        DrawRectangleRec(viewButton, CheckCollisionPointRec(mousePosition, viewButton)? LIGHTGRAY : RAYWHITE);
        DrawRectangleLinesEx(viewButton, 2.0f, DARKGRAY);
        const char *viewButtonLabel = cameraPlanView? "3D" : "Plan";
        int viewLabelWidth = MeasureText(viewButtonLabel, 18);
        DrawText(viewButtonLabel, (int)(viewButton.x + (viewButton.width - viewLabelWidth)/2.0f),
                 (int)viewButton.y + 9, 18, DARKGRAY);

        DrawFPS(10, 10);

    EndDrawing();
    //----------------------------------------------------------------------------------
}

static void UpdateCameraPosition(void)
{
    // Convert orbit yaw, elevation, and distance into a camera position around its target.
    float orbitDistance = sqrtf(cameraOrbitRadius*cameraOrbitRadius +
                                cameraOrbitHeight*cameraOrbitHeight);
    float horizontalDistance = orbitDistance*cosf(cameraElevation);
    camera.position.x = camera.target.x + horizontalDistance*sinf(cameraYaw);
    camera.position.y = camera.target.y + orbitDistance*sinf(cameraElevation);
    camera.position.z = camera.target.z + horizontalDistance*cosf(cameraYaw);

    // Keep screen-up perpendicular to the camera's viewing direction, including in Plan view.
    camera.up = (Vector3){ -sinf(cameraElevation)*sinf(cameraYaw),
                           cosf(cameraElevation),
                           -sinf(cameraElevation)*cosf(cameraYaw) };
}
