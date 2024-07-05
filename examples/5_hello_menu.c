#include "rlOpenXR.h"
#include "raylib.h"
#include "raymath.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "rlgl.h"

// Constants
const float TELEPORT_ARC_SPEED = 7.0f;
const float TELEPORT_ARC_GRAVITY = 9.81f;
#define MAX_CUBES 100

// Data structures
typedef struct
{
    XrActionSet actionset;

    XrAction hand_pose_action;
    XrPath hand_sub_paths[2];
    XrSpace hand_spaces[2];

    XrAction hand_teleport_action;
    XrAction place_cube_action;
} XRInputBindings;

typedef struct
{
    RLHand current_left_hand;
    bool active;
} Window;

typedef struct
{
    Vector3 position;
} Cube;

typedef enum { SHAPE_NONE, SHAPE_CUBE, SHAPE_SPHERE } ShapeType;

ShapeType selectedShape = SHAPE_CUBE;
Color cubeColor;
Color sphereColor;

// Function prototypes
float calculate_parabola_time_to_floor(Vector3 hand_position, Quaternion hand_orientation);
Vector3 sample_parabola_position(Vector3 hand_position, Quaternion hand_orientation, float t);

void setup_input_bindings(XRInputBindings* bindings);
void assign_hand_input_bindings(XRInputBindings* bindings, RLHand* left, RLHand* right);
void check_active_menu(XRInputBindings bindings, RLHand left, RLHand right);

// functions prototype
bool get_action_clicked_this_frame(XrAction action, XrPath sub_path);
bool is_action_active(XrAction action, XrPath sub_path);
bool CheckCollisionRayBox(Ray ray, BoundingBox box);
bool CheckCollisionRaySphere(Ray ray, Vector3 sphereCenter, float sphereRadius);

// Global variables
bool is_left_menu_active = false;
bool menu_is_placed = false;
Cube placedCubes[MAX_CUBES];
Window window;
int cubeCount = 0;

float *OurMatrixToFloat(Matrix matrix);

// Implementation
int main()
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 1200;
    const int screenHeight = 900;

    InitWindow(screenWidth, screenHeight, "rlOpenXR - Hello Menu :)");
    window.active = false;

    if (!rlOpenXRSetup())
    {
        printf("Failed to initialise rlOpenXR!");
        return 1;
    }

    Vector3 stage_position = Vector3Zero();

    Camera local_camera = {0};
    local_camera.position = (Vector3){10.0f, 10.0f, 10.0f};
    local_camera.target = (Vector3){0.0f, 3.0f, 0.0f};
    local_camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    local_camera.fovy = 45.0f;
    local_camera.projection = CAMERA_PERSPECTIVE;

    XRInputBindings bindings = {0};
    setup_input_bindings(&bindings);

    RLHand left_local_hand = {0};
    left_local_hand.handedness = RLOPENXR_HAND_LEFT;
    RLHand right_local_hand = {0};
    right_local_hand.handedness = RLOPENXR_HAND_RIGHT;
    assign_hand_input_bindings(&bindings, &left_local_hand, &right_local_hand);

    Model hand_model = LoadModelFromMesh(GenMeshCube(0.2f, 0.2f, 0.2f));

    SetCameraMode(local_camera, CAMERA_FREE);

    cubeColor = BLUE;
    sphereColor = RED;

    SetTargetFPS(-1); // OpenXR is responsible for waiting in rlOpenXRUpdate()
    // Having raylib also do it's VSync causes noticeable input lag

    //--------------------------------------------------------------------------------------

    // Main game loop
    // Main game loop
    while (!WindowShouldClose()) // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------

        rlOpenXRUpdate(); // Update OpenXR State
        // Should be called at the start of each frame before other rlOpenXR calls.

        rlOpenXRSyncSingleActionSet(bindings.actionset); // Simple utility function for simple apps with 1 action set.
        // xrSyncAction will activate action sets for use.

        rlOpenXRUpdateHands(&left_local_hand, &right_local_hand);

        UpdateCamera(&local_camera); // Use mouse control as a debug option when no HMD is available
        rlOpenXRUpdateCamera(&local_camera);
        // If the HMD is available, set the local_camera position to the HMD position

        // Camera & Hand positions we get are local to the "stage" (Area where the physical person is standing)
        // Create world space positions for rendering & gameplay
        Camera world_camera = local_camera;
        world_camera.position = Vector3Add(local_camera.position, stage_position);
        world_camera.target = Vector3Add(local_camera.target, stage_position);

        RLHand left_hand = left_local_hand;
        left_hand.position = Vector3Add(left_local_hand.position, stage_position);
        RLHand right_hand = right_local_hand;
        right_hand.position = Vector3Add(right_local_hand.position, stage_position);

        // Teleportation
        if (get_action_clicked_this_frame(bindings.hand_teleport_action, bindings.hand_sub_paths[RLOPENXR_HAND_RIGHT])
            && !is_left_menu_active)
        {
            stage_position = sample_parabola_position(right_hand.position, right_hand.orientation,
                calculate_parabola_time_to_floor(right_hand.position, right_hand.orientation));
        }

        // Place a cube or sphere
        if (get_action_clicked_this_frame(bindings.place_cube_action, bindings.hand_sub_paths[RLOPENXR_HAND_RIGHT]) &&
            cubeCount < MAX_CUBES && !is_left_menu_active)
        {
            Vector3 place_position = sample_parabola_position(right_hand.position, right_hand.orientation,
                                                              calculate_parabola_time_to_floor(
                                                                  right_hand.position, right_hand.orientation));

            if (selectedShape == SHAPE_CUBE)
            {
                placedCubes[cubeCount++] = (Cube){place_position};
            } else if (selectedShape == SHAPE_SPHERE)
            {
                // Store position for sphere
                placedCubes[cubeCount++] = (Cube){place_position};
                // Optionally you might need a separate array for spheres if they need separate treatment
            }
        }

        // Check left hand button for menu activation
        is_left_menu_active = is_action_active(bindings.hand_teleport_action, bindings.hand_sub_paths[RLOPENXR_HAND_LEFT]);

        // place window
        if (get_action_clicked_this_frame(bindings.place_cube_action, bindings.hand_sub_paths[RLOPENXR_HAND_LEFT]))
        {
            window.active = !window.active;
            window.current_left_hand = left_hand;
        }

        // Draw
        //----------------------------------------------------------------------------------
        ClearBackground(RAYWHITE);
        // Clear window, in case rlOpenXR skips rendering the frame, we don't have garbage data in the backbuffer

        // rlOpenXRBegin() returns false when OpenXR reports to skip the frame (The HMD is inactive).
        // Optionally rlOpenXRBeginMockHMD() can be chained to always render. It will render into a "Mock" backbuffer.
        if (rlOpenXRBegin() || rlOpenXRBeginMockHMD()) // Render to OpenXR backbuffer
        {
            ClearBackground(SKYBLUE);

            BeginMode3D(world_camera);

            // Draw Hands
            Vector3 left_hand_axis;
            float left_hand_angle;
            QuaternionToAxisAngle(left_hand.orientation, &left_hand_axis, &left_hand_angle);

            Vector3 right_hand_axis;
            float right_hand_angle;
            QuaternionToAxisAngle(right_hand.orientation, &right_hand_axis, &right_hand_angle);

            DrawModelEx(hand_model, left_hand.position, left_hand_axis, left_hand_angle * RAD2DEG, Vector3One(),
                        ORANGE);
            DrawModelEx(hand_model, right_hand.position, right_hand_axis, right_hand_angle * RAD2DEG, Vector3One(),
                        PINK);
            if (!is_left_menu_active)
            {
                // Draw teleportation arc
                const float t_right = calculate_parabola_time_to_floor(right_hand.position, right_hand.orientation);

                const int ARC_SEGMENTS = 50;
                for (int i = 1; i <= ARC_SEGMENTS; ++i)
                {
                    float interpolation_t_0 = t_right / ARC_SEGMENTS * (i - 1);
                    float interpolation_t_1 = t_right / ARC_SEGMENTS * i;
                    Vector3 arc_position_0 = sample_parabola_position(right_hand.position, right_hand.orientation,
                                                                      interpolation_t_0);
                    Vector3 arc_position_1 = sample_parabola_position(right_hand.position, right_hand.orientation,
                                                                      interpolation_t_1);
                    DrawCylinderEx(arc_position_0, arc_position_1, 0.05f, 0.05f, 12, DARKGREEN);
                }
            }

            // Draw Scene
            DrawCube((Vector3){-3, 0, 0}, 2.0f, 2.0f, 2.0f, RED);
            DrawGrid(10, 1.0f);

            // Draw placed cubes
            for (int i = 0; i < cubeCount; i++)
            {
                if (selectedShape == SHAPE_CUBE)
                {
                    DrawCube(placedCubes[i].position, 0.5f, 0.5f, 0.5f, BLUE);
                }
                else if (selectedShape == SHAPE_SPHERE)
                {
                    DrawSphere(placedCubes[i].position, 0.25f, RED); // Example size for sphere
                }
            }

            printf("%f %f %f\n", window.current_left_hand.orientation.x, window.current_left_hand.orientation.y, window.current_left_hand.orientation.z);
            if (window.active)
            {
                Vector3 hand_forward = Vector3RotateByQuaternion((Vector3){1, 0, 0}, window.current_left_hand.orientation);
                //printf("%f %f %f\n", hand_forward.x, hand_forward.y, hand_forward.z);
                // Vector3 rotateZ = (Vector3){0, 0, hand_forward.z};

                // Utiliser l'orientation de la main pour déterminer l'orientation du cube
                Vector3 hand_up = Vector3RotateByQuaternion((Vector3){0, 1, 0}, window.current_left_hand.orientation);
                // Vector3 rotateY = (Vector3){0, hand_forward.x, 0};
                // Matrix test = MatrixIdentity();
                // test = MatrixRotateXYZ(rotateY);

                Vector3 hand_left = Vector3RotateByQuaternion((Vector3){0, 0, 1}, window.current_left_hand.orientation);

                // Construire la matrice de transformation pour le cube
                Matrix transform = {
                    hand_forward.x, hand_forward.y, hand_forward.z, 0,
                    hand_left.x, hand_left.y, hand_left.z, 0,
                    hand_up.x, hand_up.y, hand_up.z, 0,
                    window.current_left_hand.position.x, window.current_left_hand.position.y, window.current_left_hand.position.z, 1
                };
                // Matrix transform2 = {
                //     hand_left.x, hand_left.y, hand_left.z, 0,
                //     hand_up.x, hand_up.y, hand_up.z, 0,
                //     hand_forward2.x, hand_forward2.y, hand_forward2.z, 0,
                //     menu_position.x, menu_position.y, menu_position.z, 1
                // };

                // Appliquer la matrice de transformation et dessiner le cube
                rlPushMatrix();
                rlMultMatrixf(MatrixToFloat(transform));

                // Appliquer une rotation de 45° autour de l'axe Y

                DrawCube(Vector3Zero(), 2, 1.2, 0.1, LIGHTGRAY); // Dessiner le cube centré
                rlPopMatrix();
            }

            // Draw Menu if active
            check_active_menu(bindings, left_hand, right_hand);


            EndMode3D();

            const bool keep_aspect_ratio = true;
            rlOpenXRBlitToWindow(RLOPENXR_EYE_BOTH, keep_aspect_ratio); // Copy OpenXR backbuffer to window backbuffer
            // Useful for viewing the image on a flatscreen
        }

        rlOpenXREnd();

        BeginDrawing(); // Draw to the window, eg, debug overlays

        DrawFPS(10, 10);
        DrawText(
            "Controls: \n    Teleport = Right hand 'A' button\n    Place Cube = Right hand 'B' button\n    Menu = Left hand 'X' button",
            10, 35, 20, BLACK);

        EndDrawing();
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    rlOpenXRShutdown();

    UnloadModel(hand_model);

    CloseWindow(); // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}

// Arc Math
float calculate_parabola_time_to_floor(Vector3 hand_position, Quaternion hand_orientation)
{
    // Evaluate t = -(-V0 ± sqrt(2 * g * y0 + V0^2)) / g
    // Where y0: Start height
    //		 V0: Arc initial y velocity
    //		 g: Gravity constant
    //		 t: Time along arc

    const Vector3 hand_forward = Vector3RotateByQuaternion((Vector3){0, -1, 0}, hand_orientation);
    const Vector3 initial_vel = Vector3Scale(hand_forward, TELEPORT_ARC_SPEED);

    const float g = TELEPORT_ARC_GRAVITY;

    const float t_0 =
        -(-initial_vel.y - sqrtf(2 * g * hand_position.y + initial_vel.y * initial_vel.y))
        / g;
    const float t_1 =
        -(-initial_vel.y + sqrtf(2 * g * hand_position.y + initial_vel.y * initial_vel.y))
        / g;

    return fmaxf(t_0, t_1);
}

Vector3 sample_parabola_position(Vector3 hand_position, Quaternion hand_orientation, float t)
{
    // Evaulate y = y0 + V0*t - 0.5*g*t^2
    // Where y0: Start height
    //		 V0: Arc initial y velocity
    //		 g: Gravity constant
    //		 t: Time along arc

    const Vector3 hand_forward = Vector3RotateByQuaternion((Vector3){0, -1, 0}, hand_orientation);
    const Vector3 initial_vel = Vector3Scale(hand_forward, TELEPORT_ARC_SPEED);

    const float g = TELEPORT_ARC_GRAVITY;

    const float y_at_t = hand_position.y + initial_vel.y * t - 0.5f * g * t * t;

    Vector3 sampled_position = Vector3Add(hand_position, Vector3Scale(initial_vel, t));
    sampled_position.y = y_at_t;
    return sampled_position;
}

// Input
void setup_input_bindings(XRInputBindings* bindings)
{
    const RLOpenXRData* xr = rlOpenXRData();

    XrResult result = xrStringToPath(xr->instance, "/user/hand/left", &bindings->hand_sub_paths[RLOPENXR_HAND_LEFT]);
    assert(XR_SUCCEEDED(result) && "Could not convert Left hand string to path.");
    result = xrStringToPath(xr->instance, "/user/hand/right", &bindings->hand_sub_paths[RLOPENXR_HAND_RIGHT]);
    assert(XR_SUCCEEDED(result) && "Could not convert Right hand string to path.");

    XrActionSetCreateInfo actionset_info = {0};
    actionset_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    actionset_info.next = NULL;
    strncpy(actionset_info.actionSetName, "rlopenxr_hello_hands_actionset", XR_MAX_ACTION_SET_NAME_SIZE);
    strncpy(actionset_info.localizedActionSetName, "OpenXR Hello Hands ActionSet",
            XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
    actionset_info.priority = 0;

    result = xrCreateActionSet(xr->instance, &actionset_info, &bindings->actionset);
    assert(XR_SUCCEEDED(result) && "Failed to create actionset.");

    {
        XrActionCreateInfo action_info = {0};
        action_info.type = XR_TYPE_ACTION_CREATE_INFO;
        action_info.next = NULL;
        strncpy(action_info.actionName, "handpose", XR_MAX_ACTION_NAME_SIZE);
        action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
        action_info.countSubactionPaths = RLOPENXR_HAND_COUNT;
        action_info.subactionPaths = bindings->hand_sub_paths;
        strncpy(action_info.localizedActionName, "Hand Pose", XR_MAX_LOCALIZED_ACTION_NAME_SIZE);

        result = xrCreateAction(bindings->actionset, &action_info, &bindings->hand_pose_action);
        assert(XR_SUCCEEDED(result) && "Failed to create hand pose action");
    }

    {
        XrActionCreateInfo action_info = {0};
        action_info.type = XR_TYPE_ACTION_CREATE_INFO;
        action_info.next = NULL;
        strncpy(action_info.actionName, "activate", XR_MAX_ACTION_NAME_SIZE);
        action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        action_info.countSubactionPaths = RLOPENXR_HAND_COUNT;
        action_info.subactionPaths = bindings->hand_sub_paths;
        strncpy(action_info.localizedActionName, "Activate", XR_MAX_LOCALIZED_ACTION_NAME_SIZE);

        result = xrCreateAction(bindings->actionset, &action_info, &bindings->hand_teleport_action);
        assert(XR_SUCCEEDED(result) && "Failed to create hand activate action");
    }

    {
        XrActionCreateInfo action_info = {0};
        action_info.type = XR_TYPE_ACTION_CREATE_INFO;
        action_info.next = NULL;
        strncpy(action_info.actionName, "placecube", XR_MAX_ACTION_NAME_SIZE); // New action for placing cubes
        action_info.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
        action_info.countSubactionPaths = RLOPENXR_HAND_COUNT;
        action_info.subactionPaths = bindings->hand_sub_paths;
        strncpy(action_info.localizedActionName, "Place Cube", XR_MAX_LOCALIZED_ACTION_NAME_SIZE);

        result = xrCreateAction(bindings->actionset, &action_info, &bindings->place_cube_action);
        assert(XR_SUCCEEDED(result) && "Failed to create hand place cube action");
    }

    // poses can't be queried directly, we need to create a space for each
    for (int hand = 0; hand < RLOPENXR_HAND_COUNT; hand++)
    {
        XrPosef identity_pose = {{0, 0, 0, 1}, {0, 0, 0}};

        XrActionSpaceCreateInfo action_space_info = {0};
        action_space_info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
        action_space_info.next = NULL;
        action_space_info.action = bindings->hand_pose_action;
        action_space_info.subactionPath = bindings->hand_sub_paths[hand];
        action_space_info.poseInActionSpace = identity_pose;

        result = xrCreateActionSpace(xr->session, &action_space_info, &bindings->hand_spaces[hand]);
        assert(XR_SUCCEEDED(result) && "failed to create hand %d pose space");
    }

    XrPath grip_pose_paths[2] = {0};
    xrStringToPath(xr->instance, "/user/hand/left/input/grip/pose", &grip_pose_paths[RLOPENXR_HAND_LEFT]);
    xrStringToPath(xr->instance, "/user/hand/right/input/grip/pose", &grip_pose_paths[RLOPENXR_HAND_RIGHT]);

    XrPath main_paths[2] = {0};
    xrStringToPath(xr->instance, "/user/hand/left/input/x/click", &main_paths[RLOPENXR_HAND_LEFT]);
    xrStringToPath(xr->instance, "/user/hand/right/input/a/click", &main_paths[RLOPENXR_HAND_RIGHT]);

    // Path for the new button
    XrPath secondary_paths[2] = {0};
    xrStringToPath(xr->instance, "/user/hand/left/input/y/click", &secondary_paths[RLOPENXR_HAND_LEFT]);
    xrStringToPath(xr->instance, "/user/hand/right/input/b/click", &secondary_paths[RLOPENXR_HAND_RIGHT]);

    // oculus/touch_controller Interaction Profile
    {
        XrPath interaction_profile_path;
        result = xrStringToPath(xr->instance, "/interaction_profiles/oculus/touch_controller",
                                &interaction_profile_path);
        assert(XR_SUCCEEDED(result) && "failed to get interaction profile");

        XrActionSuggestedBinding action_suggested_bindings[] = {
            {bindings->hand_pose_action, grip_pose_paths[RLOPENXR_HAND_LEFT]},
            {bindings->hand_pose_action, grip_pose_paths[RLOPENXR_HAND_RIGHT]},
            {bindings->hand_teleport_action, main_paths[RLOPENXR_HAND_LEFT]},
            {bindings->hand_teleport_action, main_paths[RLOPENXR_HAND_RIGHT]},
            {bindings->place_cube_action, secondary_paths[RLOPENXR_HAND_LEFT]}, // change rien
            {bindings->place_cube_action, secondary_paths[RLOPENXR_HAND_RIGHT]}, // Link new action to button
        };
        const int action_suggested_bindings_count = sizeof(action_suggested_bindings) / sizeof(action_suggested_bindings
            [0]);

        XrInteractionProfileSuggestedBinding suggested_bindings = {0};
        suggested_bindings.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggested_bindings.next = NULL;
        suggested_bindings.interactionProfile = interaction_profile_path;
        suggested_bindings.countSuggestedBindings = action_suggested_bindings_count;
        suggested_bindings.suggestedBindings = action_suggested_bindings;

        result = xrSuggestInteractionProfileBindings(xr->instance, &suggested_bindings);
        assert(XR_SUCCEEDED(result) && "failed to suggest bindings for oculus/touch_controller");
    }

    XrSessionActionSetsAttachInfo actionset_attach_info = {0};
    actionset_attach_info.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    actionset_attach_info.next = NULL;
    actionset_attach_info.countActionSets = 1;
    actionset_attach_info.actionSets = &bindings->actionset;
    result = xrAttachSessionActionSets(xr->session, &actionset_attach_info);
    assert(XR_SUCCEEDED(result) && "failed to attach action set");
}

void assign_hand_input_bindings(XRInputBindings* bindings, RLHand* left, RLHand* right)
{
    RLHand* hands[2] = {left, right};

    for (int i = 0; i < RLOPENXR_HAND_COUNT; ++i)
    {
        hands[i]->hand_pose_action = bindings->hand_pose_action;
        hands[i]->hand_pose_subpath = bindings->hand_sub_paths[i];
        hands[i]->hand_pose_space = bindings->hand_spaces[i];
    }
}

bool get_action_clicked_this_frame(XrAction action, XrPath sub_path)
{
    XrActionStateGetInfo activate_state_get_info;
    activate_state_get_info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    activate_state_get_info.next = NULL;
    activate_state_get_info.action = action;
    activate_state_get_info.subactionPath = sub_path;

    XrActionStateBoolean activate_state;
    activate_state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    activate_state.next = NULL;
    XrResult result = xrGetActionStateBoolean(rlOpenXRData()->session, &activate_state_get_info, &activate_state);
    assert(XR_SUCCEEDED(result) && "failed to get action state as a float");

    return activate_state.changedSinceLastSync && activate_state.currentState;
}

bool is_action_active(XrAction action, XrPath sub_path)
{
    XrActionStateGetInfo activate_state_get_info;
    activate_state_get_info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    activate_state_get_info.next = NULL;
    activate_state_get_info.action = action;
    activate_state_get_info.subactionPath = sub_path;

    XrActionStateBoolean activate_state;
    activate_state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    activate_state.next = NULL;
    XrResult result = xrGetActionStateBoolean(rlOpenXRData()->session, &activate_state_get_info, &activate_state);
    assert(XR_SUCCEEDED(result) && "failed to get action state as a float");

    return activate_state.currentState;
}

static float* OurMatrixToFloat(Matrix mat)
{
    static float result[16];
    result[0] = mat.m0;
    result[1] = mat.m4;
    result[2] = mat.m8;
    result[3] = mat.m12;
    result[4] = mat.m1;
    result[5] = mat.m5;
    result[6] = mat.m9;
    result[7] = mat.m13;
    result[8] = mat.m2;
    result[9] = mat.m6;
    result[10] = mat.m10;
    result[11] = mat.m14;
    result[12] = mat.m3;
    result[13] = mat.m7;
    result[14] = mat.m11;
    result[15] = mat.m15;
    return result;
}

bool CheckCollisionRayBox(Ray ray, BoundingBox box)
{
    Vector3 tmin = Vector3Zero();
    Vector3 tmax = Vector3Zero();
    float tymin, tymax, tzmin, tzmax;

    tmin.x = (box.min.x - ray.position.x) / ray.direction.x;
    tmax.x = (box.max.x - ray.position.x) / ray.direction.x;
    if (tmin.x > tmax.x)
    {
        float temp = tmin.x;
        tmin.x = tmax.x;
        tmax.x = temp;
    }

    tymin = (box.min.y - ray.position.y) / ray.direction.y;
    tymax = (box.max.y - ray.position.y) / ray.direction.y;
    if (tymin > tymax)
    {
        float temp = tymin;
        tymin = tymax;
        tymax = temp;
    }

    if ((tmin.x > tymax) || (tymin > tmax.x)) return false;
    if (tymin > tmin.x) tmin.x = tymin;
    if (tymax < tmax.x) tmax.x = tymax;

    tzmin = (box.min.z - ray.position.z) / ray.direction.z;
    tzmax = (box.max.z - ray.position.z) / ray.direction.z;
    if (tzmin > tzmax)
    {
        float temp = tzmin;
        tzmin = tzmax;
        tzmax = temp;
    }

    if ((tmin.x > tzmax) || (tzmin > tmax.x)) return false;
    if (tzmin > tmin.x) tmin.x = tzmin;
    if (tzmax < tmax.x) tmax.x = tzmax;

    return true;
}

bool CheckCollisionRaySphere(Ray ray, Vector3 sphereCenter, float sphereRadius)
{
    Vector3 oc = Vector3Subtract(ray.position, sphereCenter);
    float a = Vector3DotProduct(ray.direction, ray.direction);
    float b = 2.0f * Vector3DotProduct(oc, ray.direction);
    float c = Vector3DotProduct(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4 * a * c;
    return (discriminant > 0);
}

void check_active_menu(XRInputBindings bindings, RLHand left_hand, RLHand right_hand)
{
    if (is_left_menu_active)
    {
        Vector3 hand_forward = Vector3RotateByQuaternion((Vector3){0, -1, 0}, right_hand.orientation);
        // Adjust the direction vector to point forward
        Vector3 beam_start = right_hand.position;
        Vector3 beam_end = Vector3Add(beam_start, Vector3Scale(hand_forward, 5.0f)); // Extend the beam forward
        float beam_radius = 0.01f; // Define the thickness of the beam

        // Draw the beam as a cylinder
        DrawCylinderEx(beam_start, beam_end, beam_radius, beam_radius, 6, RED);

        // Define the position of the menu in front of the wrist of the left hand
        Vector3 hand_forward2 = Vector3RotateByQuaternion((Vector3){0, 0, 1}, left_hand.orientation);
        Vector3 offset = Vector3Scale(hand_forward2, -0.35f); // Offset in front of the hand
        Vector3 menu_position = Vector3Add(left_hand.position, offset);

        // Use the hand's orientation to determine the menu orientation
        Vector3 hand_up = Vector3RotateByQuaternion((Vector3){0, 1, 0}, left_hand.orientation);
        Vector3 hand_left = Vector3RotateByQuaternion((Vector3){1, 0, 0}, left_hand.orientation);

        // Construct the transformation matrix for the menu
        Matrix menu_transform = {
            hand_left.x, hand_left.y, hand_left.z, 0,
            hand_up.x, hand_up.y, hand_up.z, 0,
            hand_forward2.x, hand_forward2.y, hand_forward2.z, 0,
            menu_position.x, menu_position.y, menu_position.z, 1
        };

        // Apply the transformation matrix and draw the menu
        rlPushMatrix();
        rlMultMatrixf(OurMatrixToFloat(menu_transform));

        // Apply an additional 90 degrees rotation around the X-axis
        rlRotatef(180, 0, 1, 0);
        rlRotatef(90, 1, 0, 0);
        DrawCube((Vector3){0, 0, 0}, 0.5f, 0.5f, 0.01f, LIGHTGRAY); // Draw the menu

        // Draw additional shapes on the menu
        float shape_offset = 0.1f; // Offset to place shapes side by side

        // Initialize hit state
        bool cubeHit = false;
        bool sphereHit = false;

        // Draw and check collision for cube
        Vector3 cubePosition = (Vector3){-shape_offset, 0, 0};
        BoundingBox cubeBB = (BoundingBox){
            (Vector3){cubePosition.x - 0.1f, cubePosition.y - 0.1f, cubePosition.z - 0.1f},
            (Vector3){cubePosition.x + 0.1f, cubePosition.y + 0.1f, cubePosition.z + 0.1f}
        };
        if (CheckCollisionRayBox((Ray){right_hand.position, hand_forward2}, cubeBB))
        {
            cubeHit = true;
            cubeColor = SKYBLUE; // Highlight the cube
        }
        else
        {
            cubeColor = BLUE; // Default color
        }
        DrawCube(cubePosition, 0.1f, 0.1f, 0.1f, cubeColor);

        // Draw and check collision for sphere
        Vector3 spherePosition = (Vector3){shape_offset, 0, 0};
        if (CheckCollisionRaySphere((Ray){right_hand.position, hand_forward2}, spherePosition, 0.1f))
        {
            sphereHit = true;
            sphereColor = PINK; // Highlight the sphere
        }
        else
        {
            sphereColor = RED; // Default color
        }
        DrawSphere(spherePosition, 0.1f, sphereColor);

        rlPopMatrix();

        // Handle selection
        if (get_action_clicked_this_frame(bindings.hand_teleport_action, bindings.hand_sub_paths[RLOPENXR_HAND_RIGHT]))
        {
            if (cubeHit)
            {
                selectedShape = SHAPE_CUBE;
            }
            else if (sphereHit)
            {
                selectedShape = SHAPE_SPHERE;
            }
        }
    }
}
