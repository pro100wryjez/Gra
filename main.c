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
static int generate_stack(int min_val, int max_val, int rank, int total_ranks) {
    int range = max_val - min_val + 1;
    int upper = max_val - (rank - 1) * range / total_ranks;
    int lower = min_val + (total_ranks - rank) * range / total_ranks;
    if (lower > upper) lower = upper;
    return rand_range(lower, upper);
}

static void army_init(Army* a, int morale, int luck) {
    a->head = NULL;
    a->count = 0;
    a->morale = morale;
    a->luck = luck;
}

static void army_push_back(Army* a, const Unit* u) {
    UnitNode* n = (UnitNode*)malloc(sizeof(UnitNode));
    if (!n) {
        LOGF("Błąd: brak pamięci (malloc).\n");
        exit(1);
    }
    n->u = *u;
    n->next = NULL;

    if (!a->head) {
        a->head = n;
    }
    else {
        UnitNode* cur = a->head;
        while (cur->next) cur = cur->next;
        cur->next = n;
    }
    a->count++;
}
