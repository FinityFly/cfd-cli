#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // For fabsf, sqrtf, fmaxf, fminf

#ifdef _WIN32
#include <windows.h>
#define CLEAR_SCREEN() system("cls")
#define SLEEP_MS(ms) Sleep(ms)
void get_terminal_size(int *width, int *height) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    // Adjust height to leave room for prompt, or if full height causes scroll
    *height = csbi.srWindow.Bottom - csbi.srWindow.Top; 
    if (*width <=0) *width = 80; // Fallback
    if (*height <=0) *height = 24; // Fallback
}
#else // macOS, Linux, etc.
#include <unistd.h>    // For usleep, STDOUT_FILENO
#include <sys/ioctl.h> // For ioctl, TIOCGWINSZ, struct winsize
#define CLEAR_SCREEN() printf("\033[H\033[J") // ANSI escape for clear screen & home
#define SLEEP_MS(ms) usleep(ms * 1000)
void get_terminal_size(int *width, int *height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0) {
        // Fallback if ioctl fails
        *width = 80;
        *height = 24;
    } else {
        *width = ws.ws_col;
        *height = ws.ws_row -1; // Subtract 1 to try and leave room for prompt
    }
    if (*width <=0) *width = 80; // Extra fallback
    if (*height <=0) *height = 24; // Extra fallback
}
#endif

// Grid dimensions (will be set dynamically)
int WIDTH;
int HEIGHT;

// --- Global Simulation Parameters (with defaults) ---
float G_DT = 0.2f;                   // Time step. Critical for stability.
float G_WAVE_SPEED_SQ = 0.5f;        // Square of wave propagation "speed" (controls stiffness)
float G_DAMPING = 0.01f;             // Damping factor for wave energy (0.0 to 1.0 range for DT*Damping)
float G_INITIAL_WATER_LEVEL = 0.5f;  // Initial water level (0.0 to 1.0, where 1.0 is max cell capacity)
float G_INITIAL_TILT = 0.1f;         // Initial surface tilt (0.0 to 1.0) to start sloshing
int   G_SLEEP_MS = 50;               // Sleep time per frame in ms

// --- Grid Data (pointers for dynamic 2D arrays) ---
float **h;         // Current water height in each cell
float **vel;       // Vertical velocity of the water surface in each cell
float **next_h;    // Buffer for calculating the next height state
float **next_vel;  // Buffer for calculating the next velocity state
int   **obstacle;  // 1 if the cell is a wall, 0 if it's water

void print_usage(const char *prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("ASCII Fluid Sloshing Simulation (Heightfield Wave Method)\n");
    printf("Options:\n");
    printf("  --dt <val>             Set simulation time step (float, default: %.2f).\n"
           "                         Stability often requires (speed_sq * dt^2) <= 0.5.\n", G_DT);
    printf("  --speed_sq <val>       Set wave speed squared factor (float, default: %.2f)\n", G_WAVE_SPEED_SQ);
    printf("  --damping <val>        Set damping factor (0.0-1.0 for effective damping, default: %.3f)\n", G_DAMPING);
    printf("  --level <val>          Set initial water level (0.0-1.0, default: %.2f)\n", G_INITIAL_WATER_LEVEL);
    printf("  --tilt <val>           Set initial surface tilt (0.0-1.0, default: %.2f)\n", G_INITIAL_TILT);
    printf("  --sleep <ms>           Set sleep time per frame in ms (int, default: %d)\n", G_SLEEP_MS);
    printf("  -h, --help             Show this help message\n");
}

void allocate_grids() {
    // Allocate rows of pointers
    h = (float **)malloc(HEIGHT * sizeof(float *));
    vel = (float **)malloc(HEIGHT * sizeof(float *));
    next_h = (float **)malloc(HEIGHT * sizeof(float *));
    next_vel = (float **)malloc(HEIGHT * sizeof(float *));
    obstacle = (int **)malloc(HEIGHT * sizeof(int *));

    if (!h || !vel || !next_h || !next_vel || !obstacle) {
        fprintf(stderr, "Error: Memory allocation failed for grid pointers.\n");
        exit(EXIT_FAILURE);
    }

    // Allocate columns for each row (and initialize with calloc)
    for (int i = 0; i < HEIGHT; i++) {
        h[i] = (float *)calloc(WIDTH, sizeof(float));
        vel[i] = (float *)calloc(WIDTH, sizeof(float));
        next_h[i] = (float *)calloc(WIDTH, sizeof(float));
        next_vel[i] = (float *)calloc(WIDTH, sizeof(float));
        obstacle[i] = (int *)calloc(WIDTH, sizeof(int));
        if (!h[i] || !vel[i] || !next_h[i] || !next_vel[i] || !obstacle[i]) {
            fprintf(stderr, "Error: Memory allocation failed for grid row %d.\n", i);
            // Ideally, free already allocated rows before exiting
            exit(EXIT_FAILURE);
        }
    }
}

void free_grids() {
    if (!h) return; // Avoid freeing if allocation failed or not called
    for (int i = 0; i < HEIGHT; i++) {
        free(h[i]); free(vel[i]);
        free(next_h[i]); free(next_vel[i]);
        free(obstacle[i]);
    }
    free(h); free(vel);
    free(next_h); free(next_vel);
    free(obstacle);
}

void initialize_simulation() {
    for (int r = 0; r < HEIGHT; r++) {
        for (int c = 0; c < WIDTH; c++) {
            if (r == 0 || r == HEIGHT - 1 || c == 0 || c == WIDTH - 1) {
                obstacle[r][c] = 1; // Set border cells as obstacles (walls)
                h[r][c] = 0.0f;     // No water in walls
                vel[r][c] = 0.0f;   // No velocity in walls
            } else {
                obstacle[r][c] = 0; // Interior cells are water
                // Initial water level with a tilt factor applied across the width
                // The tilt ranges from -G_INITIAL_TILT to +G_INITIAL_TILT
                float tilt_effect = G_INITIAL_TILT * (((float)c / (WIDTH - 1.0f)) - 0.5f) * 2.0f;
                h[r][c] = G_INITIAL_WATER_LEVEL + tilt_effect;
                
                // Clamp initial height to be within [0.0, 1.0] (0% to 100% cell capacity)
                h[r][c] = fmaxf(0.0f, h[r][c]);
                h[r][c] = fminf(1.0f, h[r][c]);
                
                vel[r][c] = 0.0f;   // Initial vertical velocity is zero
            }
        }
    }
    
    // If no tilt, create a small central disturbance to start waves
    if (fabsf(G_INITIAL_TILT) < 0.001f && HEIGHT > 2 && WIDTH > 2) {
        int disturb_r = HEIGHT / 2;
        int disturb_c = WIDTH / 2;
        if (!obstacle[disturb_r][disturb_c]) { // Check if center is not an obstacle
             h[disturb_r][disturb_c] = fminf(1.0f, G_INITIAL_WATER_LEVEL + 0.4f); // Create a bump
        }
    }
}

void simulation_step() {
    for (int r = 0; r < HEIGHT; r++) {
        for (int c = 0; c < WIDTH; c++) {
            if (obstacle[r][c]) {
                next_h[r][c] = 0.0f;   // Obstacles have no water
                next_vel[r][c] = 0.0f; // And no velocity
                continue;
            }

            // Get heights of neighbors. If a neighbor is an obstacle,
            // use the current cell's height for that neighbor (simulates reflection - Neumann boundary).
            float h_up    = (r > 0          && !obstacle[r-1][c]) ? h[r-1][c] : h[r][c];
            float h_down  = (r < HEIGHT - 1 && !obstacle[r+1][c]) ? h[r+1][c] : h[r][c];
            float h_left  = (c > 0          && !obstacle[r][c-1]) ? h[r][c-1] : h[r][c];
            float h_right = (c < WIDTH - 1  && !obstacle[r][c+1]) ? h[r][c+1] : h[r][c];

            // Discrete Laplacian of the height field (measures "curvature")
            float laplacian_h = (h_up + h_down + h_left + h_right - 4.0f * h[r][c]);

            // Update velocity based on the force from the Laplacian
            float current_vel = vel[r][c];
            current_vel += (G_WAVE_SPEED_SQ * laplacian_h) * G_DT;
            
            // Apply damping to reduce wave energy over time
            current_vel *= (1.0f - G_DAMPING * G_DT); 
            next_vel[r][c] = current_vel;

            // Update height based on the new velocity
            float current_h = h[r][c];
            current_h += current_vel * G_DT; // vel is actually next_vel[r][c] after damping
            
            // Clamp height: water cannot go below 0 or above 1.0 (max cell capacity)
            current_h = fmaxf(0.0f, current_h);
            current_h = fminf(1.0f, current_h); 
            next_h[r][c] = current_h;
        }
    }

    // Swap current and next state buffers by swapping pointers (efficient)
    float **temp_ptr_h = h;
    h = next_h;
    next_h = temp_ptr_h;

    float **temp_ptr_vel = vel;
    vel = next_vel;
    next_vel = temp_ptr_vel;
}

// Convert water height (0.0 to 1.0) to an ASCII character
char height_to_char(float current_h) {
    if (current_h > 0.80f) return '@'; 
    if (current_h > 0.65f) return '#';
    if (current_h > 0.50f) return '*'; // Approx. initial level if G_INITIAL_WATER_LEVEL is 0.5
    if (current_h > 0.35f) return '='; 
    if (current_h > 0.20f) return '-';
    if (current_h > 0.05f) return '.'; // Shallow
    return ' '; // Empty or very shallow
}

void display_grid() {
    CLEAR_SCREEN();
    // Prepare a buffer for the entire screen content to print in one go (reduces flicker)
    int buffer_len = (WIDTH + 1) * HEIGHT + 1; // +1 for newline per row, +1 for null terminator
    char *screen_buffer = (char *)malloc(buffer_len);
    
    if (!screen_buffer) { // Fallback to direct printf if buffer allocation fails
        for (int r = 0; r < HEIGHT; r++) {
            for (int c = 0; c < WIDTH; c++) {
                if (obstacle[r][c]) {
                    printf("X"); // Obstacle character
                } else {
                    printf("%c", height_to_char(h[r][c]));
                }
            }
            printf("\n");
        }
    } else {
        char *current_char_ptr = screen_buffer;
        for (int r = 0; r < HEIGHT; r++) {
            for (int c = 0; c < WIDTH; c++) {
                if (obstacle[r][c]) {
                    *current_char_ptr++ = 'X';
                } else {
                    *current_char_ptr++ = height_to_char(h[r][c]);
                }
            }
            *current_char_ptr++ = '\n'; // Newline after each row
        }
        *current_char_ptr = '\0'; // Null-terminate the buffer
        printf("%s", screen_buffer);
        free(screen_buffer);
    }
    fflush(stdout); // Ensure output is flushed
}

int main(int argc, char *argv[]) {
    // Argument Parsing
    for (int k = 1; k < argc; ++k) {
        if (strcmp(argv[k], "--dt") == 0) {
            if (++k < argc) G_DT = atof(argv[k]); else { print_usage(argv[0]); return 1; }
        } else if (strcmp(argv[k], "--speed_sq") == 0) {
            if (++k < argc) G_WAVE_SPEED_SQ = atof(argv[k]); else { print_usage(argv[0]); return 1; }
        } else if (strcmp(argv[k], "--damping") == 0) {
            if (++k < argc) G_DAMPING = atof(argv[k]); else { print_usage(argv[0]); return 1; }
        } else if (strcmp(argv[k], "--level") == 0) {
            if (++k < argc) G_INITIAL_WATER_LEVEL = atof(argv[k]); else { print_usage(argv[0]); return 1; }
        } else if (strcmp(argv[k], "--tilt") == 0) {
            if (++k < argc) G_INITIAL_TILT = atof(argv[k]); else { print_usage(argv[0]); return 1; }
        } else if (strcmp(argv[k], "--sleep") == 0) {
            if (++k < argc) G_SLEEP_MS = atoi(argv[k]); else { print_usage(argv[0]); return 1; }
        } else if (strcmp(argv[k], "-h") == 0 || strcmp(argv[k], "--help") == 0) {
            print_usage(argv[0]); return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[k]);
            print_usage(argv[0]); return 1;
        }
    }

    // Validate parsed parameters
    if (G_DT <= 0) { fprintf(stderr, "Error: dt must be > 0.\n"); return 1; }
    if (G_WAVE_SPEED_SQ <= 0) { fprintf(stderr, "Error: speed_sq must be > 0.\n"); return 1; }
    // G_DAMPING * G_DT should ideally be < 1 for stable damping. Here G_DAMPING is the factor.
    if (G_DAMPING < 0.0f ) { fprintf(stderr, "Error: damping must be >= 0.0.\n"); return 1; }
    if (G_INITIAL_WATER_LEVEL < 0.0f || G_INITIAL_WATER_LEVEL > 1.0f) { fprintf(stderr, "Error: level must be 0.0-1.0.\n"); return 1; }
    if (G_INITIAL_TILT < 0.0f || G_INITIAL_TILT > 1.0f) { fprintf(stderr, "Error: tilt must be 0.0-1.0.\n"); return 1; }
    if (G_SLEEP_MS < 0) { fprintf(stderr, "Error: sleep ms must be >= 0.\n"); return 1; }

    // Check a common stability condition for this explicit finite difference scheme (Courant-Friedrichs-Lewy like)
    // For a 2D wave equation with 5-point Laplacian, (c*DT/dx)^2 <= 0.5 often applies.
    // Here, c^2 is G_WAVE_SPEED_SQ, and dx (cell spacing) is implicitly 1.
    float stability_metric = G_WAVE_SPEED_SQ * G_DT * G_DT;
    if (stability_metric > 0.5f) {
        fprintf(stderr, "Warning: Simulation might be unstable!\n");
        fprintf(stderr, "         (speed_sq * dt^2) = %.3f. For stability, this value should ideally be <= 0.5.\n", stability_metric);
        fprintf(stderr, "         Consider reducing dt or speed_sq.\n");
    }

    get_terminal_size(&WIDTH, &HEIGHT);
    if (WIDTH < 10 || HEIGHT < 5) { // Ensure a minimum usable size
        fprintf(stderr, "Terminal too small. Minimum 10x5 required. Using fallback 20x10.\n");
        WIDTH = (WIDTH < 10) ? 20 : WIDTH; 
        HEIGHT = (HEIGHT < 5) ? 10 : HEIGHT;
    }
    
    printf("Terminal: %dx%d. Starting fluid sloshing simulation...\n", WIDTH, HEIGHT);
    printf("Parameters: DT=%.3f, SpeedSq=%.2f, Damping=%.3f, Level=%.2f, Tilt=%.2f, Sleep=%dms\n",
           G_DT, G_WAVE_SPEED_SQ, G_DAMPING, G_INITIAL_WATER_LEVEL, G_INITIAL_TILT, G_SLEEP_MS);
    if (stability_metric > 0.5f) printf("WARNING: POTENTIAL INSTABILITY (see details above)\n");
    SLEEP_MS(3000); // Give time to read parameters and warnings

    allocate_grids();
    initialize_simulation();

    // Main simulation loop
    while (1) {
        simulation_step();
        display_grid();
        SLEEP_MS(G_SLEEP_MS);
    }

    free_grids(); // Technically unreachable due to infinite loop, but good practice
    return 0;
}
