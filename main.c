#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAX_NAME   40
#define MAX_READY  10

#define UNITS_FILE "units.txt"
#define LOG_FILE   "battle_log.txt"
#define SUMMARY_FILE "summary.txt"

static FILE* g_log = NULL;

#define LOGF(...) do { \
    printf(__VA_ARGS__); \
    if (g_log) fprintf(g_log, __VA_ARGS__); \
} while(0)

typedef struct {
    char name[MAX_NAME];
    int attack;
    int defense;
    int min_damage;
    int max_damage;
    int hp;
    int initiative;
    int power;
    int stack;
    int current_hp;
    double readiness;
    bool alive;
    bool countered;
    bool defended;
} Unit;

/* ====== LISTA DYNAMICZNA ====== */
typedef struct UnitNode {
    Unit u;
    struct UnitNode* next;
} UnitNode;

typedef struct {
    UnitNode* head;
    int count;
    int morale;
    int luck;
} Army;

/* --- Funkcje pomocnicze --- */
static int rand_range(int min, int max) {
    return min + rand() % (max - min + 1);
}
