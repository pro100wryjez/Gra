#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h> // true/false
#include <string.h>
#include <time.h>
#include <stdarg.h> // obsługa zmiennej liczby argumentów

// komendy do animacji ataku
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define MAX_NAME   40 
#define MAX_READY  10

#define UNITS_FILE "units.txt" // plik z jednostkami
#define LOG_FILE   "battle_log.txt" // logi
#define SUMMARY_FILE "summary.txt" // podsumowanie bitwy

static FILE* g_log = NULL; // log do komendy poniżej
// komenda, która pozwala zapisywać tekst na żywo, a także do pliku
#define LOGF(...) do {\
    printf(__VA_ARGS__); \
    if (g_log) fprintf(g_log, __VA_ARGS__); \
} while(0)


typedef struct { // opis jednostki
    char name[MAX_NAME]; 
    int attack;
    int defense;
    int min_damage;
    int max_damage;
    int hp;
    int initiative;
    int power;
    int stack; // AI używa do priorytetu celu
    int current_hp; // hp ostatniej zyjacej istoty
    double readiness; // gotowosc bojowa
    bool alive; 
    bool countered;
    bool defended;
} Unit;

typedef struct UnitNode { // lista z jednostkami
    Unit u;
    struct UnitNode* next;
} UnitNode;

typedef struct { // lista, ilosc, morale, luck
    UnitNode* head;
    int count;
    int morale;
    int luck;
} Army;

static int rand_range(int min, int max) {
    return min + rand() % (max - min + 1);
}

static int generate_stack(int min_val, int max_val, int rank, int total_ranks) { // generator jednostek, dzięki niemu w słabszych jednostek jest więcej, silniejszych mniej.
    int range = max_val - min_val + 1;
    int upper = max_val - (rank - 1) * range / total_ranks;
    int lower = min_val + (total_ranks - rank) * range / total_ranks;
    if (lower > upper) lower = upper;
    return rand_range(lower, upper);
}

static void army_init(Army* a, int morale, int luck) { // ustawia armię w stanie początkowym (pusta lista, morale, szczęście)

    a->head = NULL;
    a->count = 0;
    a->morale = morale;
    a->luck = luck;
}

static void army_push_back(Army* a, const Unit* u) { // dodaje nową jednostkę do armii
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

static void army_free(Army* a) { // zwalnia pamięć zajętą przez jednostki armii
    UnitNode* cur = a->head;
    while (cur) {
        UnitNode* next = cur->next;
        free(cur);
        cur = next;
    }
    a->head = NULL;
    a->count = 0;
}

static bool all_dead(const Army* army) { // sprawdza, czy wszystkie jednostki w armii są martwe
    for (UnitNode* n = army->head; n; n = n->next)
        if (n->u.alive) return false;
    return true;
}

static Unit* choose_enemy_target(Army* enemy) { // AI wybiera cel o największym power * stack
    int max_power = -1;
    Unit* target = NULL;
    for (UnitNode* n = enemy->head; n; n = n->next) {
        if (n->u.alive) {
            int unit_power = n->u.power * n->u.stack;
            if (unit_power > max_power) {
                max_power = unit_power;
                target = &n->u;
            }
        }
    }
    return target;
}

static Unit* choose_alive_target_ptr(Army* enemy) { // wybór atakowanej jednostki przez gracza
    while (1) {
        int k = 0;
        LOGF("Wybierz cel:\n");
        for (UnitNode* n = enemy->head; n; n = n->next) {
            if (n->u.alive) {
                k++;
                LOGF("%d: %s (Stack: %d, HP: %d)\n", k, n->u.name, n->u.stack, n->u.current_hp);
            }
        }
        if (k == 0) return NULL;

        int choice = 0;
        LOGF("Twój wybór: ");
        if (scanf("%d", &choice) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            LOGF("Nieprawidłowy input.\n");
            continue;
        }

        if (choice < 1 || choice > k) {
            LOGF("Nieprawidłowy wybór, spróbuj ponownie.\n");
            continue;
        }

        // przejście drugi raz po liście i znalezienie wybranego żywego 
        int idx = 0;
        for (UnitNode* n = enemy->head; n; n = n->next) {
            if (n->u.alive) {
                idx++;
                if (idx == choice) return &n->u;
            }
        }
        LOGF("Nieprawidłowy wybór, spróbuj ponownie.\n");
    }
}

static void show_unit(Unit u) { // wyświetlanie statystyk jednostki

    if (u.alive)
        LOGF("%s | Atak: %d | Obrona: %d | Obrażenia: %d-%d | HP: %d | Inicjatywa: %d | Stack: %d\n",
            u.name, u.attack, u.defense, u.min_damage, u.max_damage, u.hp, u.initiative, u.stack);
    else
        LOGF("%s (DEAD)\n", u.name);
}

static void show_army(const Army* army) {
    for (UnitNode* n = army->head; n; n = n->next)
        show_unit(n->u);
}

static void show_summary(const Army* army, const char* title) {
    LOGF("\n=== PODSUMOWANIE: %s ===\n", title);
    for (UnitNode* n = army->head; n; n = n->next) {
        Unit u = n->u;
        if (u.alive)
            LOGF("%s | Stack: %d | HP: %d\n", u.name, u.stack, u.current_hp);
        else
            LOGF("%s (DEAD) | Stack: 0 | HP: 0\n", u.name);
    }
}

static void attack_animation(const char* attacker_name, const char* defender_name, bool counter) { // animacja ataku
    if (counter) LOGF("Kontratak");
    else LOGF("Atak");
    fflush(stdout);
    if (g_log) fflush(g_log);

    for (int i = 0; i < 3; i++) {
#ifdef _WIN32
        Sleep(200);
#else
        usleep(200000);
#endif
        LOGF(".");
        fflush(stdout);
        if (g_log) fflush(g_log);
    }
    LOGF("\n");

    if (counter)
        LOGF("%s kontratakuje %s\n", attacker_name, defender_name);
    else
        LOGF("%s atakuje %s\n", attacker_name, defender_name);

    for (int i = 0; i < 5; i++) {
        LOGF("   ATAK!");
        fflush(stdout);
        if (g_log) fflush(g_log);
#ifdef _WIN32
        Sleep(150);
#else
        usleep(150000);
#endif
    }
    LOGF("\n");
}

static void attack_with_counter(Unit* attacker, Unit* defender, int attacker_luck, int defender_luck) { // atak jednostki z uwzględnieniem obrony, szczęścia i jednorazowego kontrataku

    if (!attacker || !defender) return;
    if (!attacker->alive || !defender->alive) return;

    attack_animation(attacker->name, defender->name, false);

    int single_unit_damage = rand_range(attacker->min_damage, attacker->max_damage);
    double base_damage = (double)single_unit_damage * attacker->stack;

    double defense_modifier = 0.06 * defender->defense;
    if (defense_modifier > 0.7) defense_modifier = 0.7;

    double damage = base_damage * (1.0 - defense_modifier);
    if (damage < base_damage * 0.65) damage = base_damage * 0.65;
    if (damage < 1) damage = 1;

    if (attacker_luck != 0) {
        int roll = rand_range(1, 10);
        if (attacker_luck > 0 && roll <= attacker_luck * 2) {
            damage *= 1.5;
            LOGF("%s korzysta z POZYTYWNEGO szczęścia! (+50%% obrażeń)\n", attacker->name);
        }
        else if (attacker_luck < 0 && roll <= -attacker_luck * 2) {
            damage *= 0.5;
            LOGF("%s doświadcza NEGATYWNEGO szczęścia! (-50%% obrażeń)\n", attacker->name);
        }
    }

    int kills = (int)(damage / defender->hp);
    if (kills > defender->stack) kills = defender->stack;

    defender->stack -= kills;
    if (defender->stack <= 0) {
        defender->stack = 0;
        defender->alive = false;
        defender->current_hp = 0;
        LOGF("Zabija %d jednostek, Pozostało: 0, HP: 0\n\n", kills);
        return;
    }
    else {
        defender->current_hp = (int)(damage - (double)kills * defender->hp);
        if (defender->current_hp < 0) defender->current_hp = 0;
    }

    LOGF("Zabija %d jednostek, Pozostało: %d, HP: %d\n\n", kills, defender->stack, defender->current_hp);

    if (defender->alive && defender->countered == false) { // kontraatak
        defender->countered = true;
        attack_animation(defender->name, attacker->name, true);

        single_unit_damage = rand_range(defender->min_damage, defender->max_damage);
        base_damage = (double)single_unit_damage * defender->stack;

        defense_modifier = 0.07 * attacker->defense;
        if (defense_modifier > 0.55) defense_modifier = 0.55;

        damage = base_damage * (1.0 - defense_modifier);
        if (damage < base_damage * 0.4) damage = base_damage * 0.4;
        if (damage < 1) damage = 1;

        if (defender_luck != 0) {
            int roll = rand_range(1, 10);
            if (defender_luck > 0 && roll <= defender_luck * 2) {
                damage *= 1.5;
                LOGF("%s korzysta z POZYTYWNEGO szczęścia! (+50%% obrażeń)\n", defender->name);
            }
            else if (defender_luck < 0 && roll <= -defender_luck * 2) {
                damage *= 0.5;
                LOGF("%s doświadcza NEGATYWNEGO szczęścia! (-50%% obrażeń)\n", defender->name);
            }
        }

        int counter_kills = (int)(damage / attacker->hp);
        if (counter_kills > attacker->stack) counter_kills = attacker->stack;

        attacker->stack -= counter_kills;
        if (attacker->stack <= 0) {
            attacker->stack = 0;
            attacker->alive = false;
            attacker->current_hp = 0;
        }
        else {
            attacker->current_hp = (int)(damage - (double)counter_kills * attacker->hp);
            if (attacker->current_hp < 0) attacker->current_hp = 0;
        }

        LOGF("Zabija %d jednostek, Pozostało: %d, HP: %d\n\n", counter_kills, attacker->stack, attacker->current_hp);
    }
}

static void show_actions(Unit* u) {
    LOGF("\nAkcje dla jednostki %s (Stack: %d, HP: %d):\n", u->name, u->stack, u->current_hp);
    LOGF("1: Atak\n");
    LOGF("2: Obrona (+30%% obrony, tylko raz, -10 gotowości)\n");
    LOGF("3: Czekaj (-5 gotowości)\n");
    LOGF("4: Ucieczka (natychmiastowa przegrana)\n");
}

static void player_turn(Unit* u, Army* enemy, int morale, int luck, bool* escape_flag) { // tura gracza
    if (!u->alive || u->readiness < MAX_READY) return;

    int morale_roll = rand_range(1, 10);
    bool skip_turn = false;
    bool double_turn = false;

    if (morale != 0) {
        if (morale > 0 && morale_roll <= morale) {
            double_turn = true;
            LOGF("%s otrzymuje POZYTYWNE morale! (dwa ruchy z rzędu)\n", u->name);
        }
        else if (morale < 0 && morale_roll <= -morale) {
            skip_turn = true;
            u->readiness /= 2;
            LOGF("%s otrzymuje NEGATYWNE morale! (traci turę, gotowość bojowa zmniejszona o 50%%)\n", u->name);
        }
    }

    if (skip_turn) return;

    int actions = double_turn ? 2 : 1;
    for (int a = 0; a < actions; a++) {
        int choice;
        while (1) {
            show_actions(u);
            LOGF("Twój wybór: ");
            if (scanf("%d", &choice) != 1) {
                int c;
                while ((c = getchar()) != '\n' && c != EOF) {}
                LOGF("Nieprawidłowy input.\n");
                continue;
            }

            if (choice == 1) {
                Unit* target = choose_alive_target_ptr(enemy);
                attack_with_counter(u, target, luck, enemy->luck);
                u->countered = false;
                u->readiness -= 10;
                if (u->readiness < 0) u->readiness = 0;
                break;
            }
            else if (choice == 2) {
                if (!u->defended) {
                    int bonus = u->defense * 30 / 100;
                    if (bonus < 1) bonus = 1;
                    u->defense += bonus;
                    u->defended = true;
                    u->countered = false;
                    LOGF("%s broni się... 🛡️ (+%d obrony)\n", u->name, bonus);
                    u->readiness -= 10;
                    if (u->readiness < 0) u->readiness = 0;
                    break;
                }
                else {
                    LOGF("%s już użył obrony wcześniej.\n", u->name);
                }
            }
            else if (choice == 3) {
                LOGF("%s czeka... ⏳\n", u->name);
                u->countered = false;
                u->readiness -= 5;
                if (u->readiness < 0) u->readiness = 0;
                break;
            }
            else if (choice == 4) {
                LOGF("%s decyduje się uciec! Bitwa zakończona przegraną.\n", u->name);
                *escape_flag = true;
                return;
            }
            else {
                LOGF("Nieprawidłowy wybór, spróbuj ponownie.\n");
            }
        }
    }
}

static void enemy_turn(Unit* u, Army* player, int morale, int luck) { // tura ai
    if (!u->alive || u->readiness < MAX_READY) return;

    int morale_roll = rand_range(1, 10);
    bool skip_turn = false;
    bool double_turn = false;

    if (morale != 0) {
        if (morale > 0 && morale_roll <= morale) {
            double_turn = true;
            LOGF("%s otrzymuje POZYTYWNE morale! (dwa ruchy z rzędu)\n", u->name);
        }
        else if (morale < 0 && morale_roll <= -morale) {
            skip_turn = true;
            u->readiness /= 2;
            LOGF("%s otrzymuje NEGATYWNE morale! (traci turę, gotowość bojowa zmniejszona o 50%%)\n", u->name);
        }
    }

    if (skip_turn) return;

    int actions = double_turn ? 2 : 1;
    for (int a = 0; a < actions; a++) {
        if (u->current_hp < u->hp / 2 && !u->defended) {
            int bonus = u->defense * 30 / 100;
            if (bonus < 1) bonus = 1;
            u->defense += bonus;
            u->defended = true;
            u->countered = false;
            LOGF("%s broni się... 🛡️ (+%d obrony)\n", u->name, bonus);
            u->readiness -= 10;
            if (u->readiness < 0) u->readiness = 0;
            continue;
        }

        Unit* target = choose_enemy_target(player);
        if (target != NULL)
            attack_with_counter(u, target, luck, player->luck);

        u->countered = false;
        u->readiness -= 10;
        if (u->readiness < 0) u->readiness = 0;
    }
}

static void battle(Army* player, Army* enemy) { // walka
    bool first_turn = true;
    bool escape = false;

    while (true) {
        if (all_dead(enemy)) {
            LOGF("\nZWYCIĘSTWO!\n");
            break;
        }
        if (all_dead(player) || escape) {
            if (escape) LOGF("\nGRACZ UCIEKŁ! BITWA ZAKOŃCZONA PRZEGRANĄ.\n");
            else LOGF("\nPORAŻKA!\n");
            break;
        }

        if (first_turn) {
            for (UnitNode* n = player->head; n; n = n->next) n->u.readiness = n->u.initiative;
            for (UnitNode* n = enemy->head; n; n = n->next) n->u.readiness = n->u.initiative;
            first_turn = false;
        }
        else {
            for (UnitNode* n = player->head; n; n = n->next)
                if (n->u.alive) n->u.readiness += n->u.initiative / 10.0;
            for (UnitNode* n = enemy->head; n; n = n->next)
                if (n->u.alive) n->u.readiness += n->u.initiative / 10.0;
        }

        for (UnitNode* n = player->head; n; n = n->next) {
            player_turn(&n->u, enemy, player->morale, player->luck, &escape);
            if (escape) break;
        }
        if (escape) break;

        for (UnitNode* n = enemy->head; n; n = n->next)
            enemy_turn(&n->u, player, enemy->morale, enemy->luck);
    }

    show_summary(player, "TWOJA ARMIA (po bitwie)");
    show_summary(enemy, "ARMIA WROGA (po bitwie)");

    if (escape) exit(0);
}

static void write_default_units_file(void) { // tworzenie units.txt
    FILE* f = fopen(UNITS_FILE, "w");
    if (!f) return;

    // Format: SIDE;NAME;ATK;DEF;MIN;MAX;HP;INIT;POWER
    fprintf(f, "# SIDE;NAME;ATK;DEF;MIN;MAX;HP;INIT;POWER\n");
    fprintf(f, "P;Rekrut;2;1;1;3;5;8;72\n");
    fprintf(f, "P;Łucznik precyzyjny;1;4;2;10;11;8;199\n");
    fprintf(f, "P;Rycerz Kruczy;4;11;2;6;30;8;201\n");
    fprintf(f, "P;Sokół Szlachetny;13;7;5;17;40;15;716\n");
    fprintf(f, "P;Strażnik Światła;18;18;11;12;83;10;1086\n");
    fprintf(f, "P;Wojownik Zakonu;27;20;15;35;105;12;2185\n");
    fprintf(f, "P;Nieśmiertelny Anioł;40;25;34;67;220;11;6153\n");

    fprintf(f, "E;Istota Z Otchłani;4;1;2;5;6;15;127\n");
    fprintf(f, "E;Bestia Płomieni;5;2;1;6;16;8;150\n");
    fprintf(f, "E;Zły Poplecznik;7;2;4;5;18;13;338\n");
    fprintf(f, "E;Mroczna Królowa;6;6;6;16;30;10;694\n");
    fprintf(f, "E;Czarny Koń;16;23;10;12;80;16;1415\n");
    fprintf(f, "E;Władca Spopielenia;25;20;11;35;125;8;2360\n");
    fprintf(f, "E;Herold Zagłady;35;30;30;59;205;11;5850\n");

    fclose(f);
}

static bool load_armies_from_file(Army* player, Army* enemy) {
    FILE* f = fopen(UNITS_FILE, "r");
    if (!f) {
        LOGF("Brak %s — tworzę domyślny plik.\n", UNITS_FILE);
        write_default_units_file();
        f = fopen(UNITS_FILE, "r");
        if (!f) {
            LOGF("Nie mogę otworzyć %s.\n", UNITS_FILE);
            return false;
        }
    }

    char line[512]; // wczytywanie jednostek z units.txt
    int rankP = 0, rankE = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        line[strcspn(line, "\r\n")] = 0;

        char* tok = strtok(line, ";");
        if (!tok) continue;
        char side = tok[0];

        char* name = strtok(NULL, ";");
        char* atk = strtok(NULL, ";");
        char* def = strtok(NULL, ";");
        char* minD = strtok(NULL, ";");
        char* maxD = strtok(NULL, ";");
        char* hp = strtok(NULL, ";");
        char* init = strtok(NULL, ";");
        char* pow = strtok(NULL, ";");

        if (!name || !atk || !def || !minD || !maxD || !hp || !init || !pow) continue;

        Unit u;
        memset(&u, 0, sizeof(u));
        strncpy(u.name, name, MAX_NAME - 1);
        u.attack = atoi(atk);
        u.defense = atoi(def);
        u.min_damage = atoi(minD);
        u.max_damage = atoi(maxD);
        u.hp = atoi(hp);
        u.current_hp = u.hp;
        u.initiative = atoi(init);
        u.power = atoi(pow);

        u.readiness = 0;
        u.alive = true;
        u.countered = false;
        u.defended = false;

        if (side == 'P') {
            rankP++;
            u.stack = generate_stack(1, 300, rankP, 7);
            army_push_back(player, &u);
        }
        else if (side == 'E') {
            rankE++;
            u.stack = generate_stack(1, 300, rankE, 7);
            army_push_back(enemy, &u);
        }
    }

    fclose(f);

    if (player->count == 0 || enemy->count == 0) {
        LOGF("Błąd: nie wczytano jednostek (sprawdź format %s).\n", UNITS_FILE);
        return false;
    }
    return true;
}

static void save_summary_to_file(const Army* player, const Army* enemy) {
    FILE* f = fopen(SUMMARY_FILE, "w");
    if (!f) return;

    fprintf(f, "PODSUMOWANIE BITWY\n\n");

    fprintf(f, "TWOJA ARMIA:\n");
    for (UnitNode* n = player->head; n; n = n->next) {
        Unit u = n->u;
        fprintf(f, "%s | alive=%d | stack=%d | hp=%d\n", u.name, (int)u.alive, u.stack, u.current_hp);
    }

    fprintf(f, "\nARMIA WROGA:\n");
    for (UnitNode* n = enemy->head; n; n = n->next) {
        Unit u = n->u;
        fprintf(f, "%s | alive=%d | stack=%d | hp=%d\n", u.name, (int)u.alive, u.stack, u.current_hp);
    }

    fclose(f);
}

int main(void) {
    srand((unsigned)time(NULL));

    g_log = fopen(LOG_FILE, "w"); 
    if (!g_log) {
        printf("Uwaga: nie mogę utworzyć %s (log będzie tylko na ekranie).\n", LOG_FILE);
    }

    Army* player = (Army*)malloc(sizeof(Army));
    Army* enemy = (Army*)malloc(sizeof(Army));
    if (!player || !enemy) {
        LOGF("Błąd: brak pamięci na armie.\n");
        return 1;
    }

    army_init(player, rand_range(-5, 5), rand_range(-5, 5));
    army_init(enemy, rand_range(-5, 5), rand_range(-5, 5));

    if (!load_armies_from_file(player, enemy)) {
        army_free(player);
        army_free(enemy);
        free(player);
        free(enemy);
        if (g_log) fclose(g_log);
        return 1;
    }

    LOGF("Twoje morale: %d, szczęście: %d\n", player->morale, player->luck);
    LOGF("Wrogie morale: %d, szczęście: %d\n\n", enemy->morale, enemy->luck);

    LOGF("=== STATYSTYKI TWOJEJ ARMII ===\n"); show_army(player);
    LOGF("\n=== STATYSTYKI ARMII WROGA ===\n"); show_army(enemy);

    battle(player, enemy);

    save_summary_to_file(player, enemy);

    army_free(player);
    army_free(enemy);
    free(player);
    free(enemy);

    if (g_log) fclose(g_log);

    return 0;
}
