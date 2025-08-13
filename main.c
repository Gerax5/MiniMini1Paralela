#include <stdbool.h>

typedef enum {
    RED,
    YELLOW,
    GREEN
} lightColors;

typedef struct {
    int id;

} interseccion;

typedef struct {
    int id;
    int lane;      
    int pos;      
    int goalLane;  
    bool arrived;
} Vehicle;