/* test_settings.c — the defaults, /coco/coco.ini's parser, and the save
 * that edits it (settings.h, design.md §12.6; EL §8.7). Needs no ROM.
 * Brought from pico-oric with its keys changed for the CoCo's.
 */

#include <string.h>

#include "settings.h"
#include "test_util.h"

static settings_status_t parse(settings_t *s, const char *text, unsigned *line) {
    settings_default(s);
    return settings_parse(s, text, strlen(text), line);
}

int main(void) {
    settings_t s, d;
    unsigned line;

    /* ---- the defaults are the machine's and the host's ---------------- */
    settings_default(&d);
    {
        coco_config_t cfg;
        coco_config_default(&cfg);
        CHECK(d.extended && d.ram_k == cfg.ram_k && d.ram_k == 64u,
              "Extended Color BASIC in 64 KB (D6)");
        CHECK(d.volume == 8u && !d.perf && d.status && d.backlight == 0u,
              "full volume, no perf line, the status line, the panel's backlight");
        CHECK(!d.cart[0] && !d.boot_tape[0], "no cartridge, no tape");
    }

    /* ---- an empty file, and one of comments, change nothing ----------- */
    CHECK(parse(&s, "", &line) == SET_OK && line == 0, "empty");
    CHECK(memcmp(&s, &d, sizeof s) == 0, "empty changes nothing");
    CHECK(parse(&s, "# nothing\n\n   # indented\r\n", &line) == SET_OK && line == 0, "comments");
    CHECK(memcmp(&s, &d, sizeof s) == 0, "comments change nothing");

    /* ---- every setting ------------------------------------------------ */
    {
        const char *all =
            "# every key\r\n"
            "BASIC     = Color\r\n"
            "RAM       = 16\r\n"
            "cart      = Dungeons of Daggorath.ccc\r\n"
            "volume    = 0\r\n"
            "Perf      = On\r\n"
            "status    = off\r\n"
            "backlight = 15\r\n"
            "boot_tape = /coco/tapes/My Tape.cas\r\n";
        CHECK(parse(&s, all, &line) == SET_OK && line == 0, "every setting: line %u", line);
        CHECK(!s.extended && s.ram_k == 16u, "basic, ram");
        CHECK(s.volume == 0u && s.perf && !s.status && s.backlight == 15u,
              "volume, perf, status, backlight");
        CHECK(strcmp(s.cart, "Dungeons of Daggorath.ccc") == 0, "cart: %s", s.cart);
        CHECK(strcmp(s.boot_tape, "/coco/tapes/My Tape.cas") == 0, "boot_tape: %s", s.boot_tape);
    }
    for (unsigned k = 0; k < 4; k++) {
        static const char *const t[] = { "ram = 4\n", "ram = 16\n", "ram = 32\n", "ram = 64\n" };
        static const unsigned v[] = { 4u, 16u, 32u, 64u };
        CHECK(parse(&s, t[k], &line) == SET_OK && s.ram_k == v[k], "%s", t[k]);
    }
    CHECK(parse(&s, "basic = extended\n", &line) == SET_OK && s.extended, "extended by name");
    CHECK(parse(&s, "ram = 16\nram2 = x\n", &line) == SET_UNKNOWN && s.ram_k == 16u,
          "16 KB, with a stranger after it");
    CHECK(parse(&s, "cart =\n", &line) == SET_OK && !s.cart[0], "no cartridge");
    CHECK(parse(&s, "boot_tape =   \n", &line) == SET_OK && !s.boot_tape[0], "empty means none");

    /* ---- comments after a value --------------------------------------- */
    CHECK(parse(&s, "ram = 16   # as sold\r\n"
                    "volume = 3\t# 0-8\n"
                    "boot_tape =          # none\n", &line) == SET_OK && line == 0,
          "trailing comments: line %u", line);
    CHECK(s.ram_k == 16u && s.volume == 3u && !s.boot_tape[0], "trailing comments stripped");
    CHECK(parse(&s, "boot_tape = side#2.cas # the second\n", &line) == SET_OK &&
          strcmp(s.boot_tape, "side#2.cas") == 0, "a # inside a path is kept: %s", s.boot_tape);
    CHECK(parse(&s, "ram # = 16\n", &line) == SET_SYNTAX && line == 1,
          "a comment before the = leaves no value");

    /* ---- a bad line changes nothing, and the rest still apply --------- */
    {
        static const struct { const char *text; settings_status_t st; } bad[] = {
            { "ram 16\n",              SET_SYNTAX },
            { "= 16\n",                SET_SYNTAX },
            { "ram =\n",               SET_SYNTAX },
            { "basic =\n",             SET_SYNTAX },
            { "memory = 16\n",         SET_UNKNOWN },
            { "ram = 8\n",             SET_BAD_VALUE },
            { "ram = 16k\n",           SET_BAD_VALUE },
            { "ram = 128\n",           SET_BAD_VALUE },
            { "basic = disk\n",        SET_BAD_VALUE },   /* M13 */
            { "basic = colour\n",      SET_BAD_VALUE },   /* the ROM's own spelling */
            { "volume = 9\n",          SET_BAD_VALUE },
            { "volume = -1\n",         SET_BAD_VALUE },
            { "volume = 99999999999\n", SET_BAD_VALUE },
            { "perf = yes\n",          SET_BAD_VALUE },
            { "machine = dragon32\n",  SET_UNKNOWN },     /* the CoCo only (D6) */
            { "layout = games\n",      SET_UNKNOWN },     /* M12 */
        };
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            settings_status_t st = parse(&s, bad[i].text, &line);
            CHECK(st == bad[i].st && line == 1, "%s-> %s, line %u", bad[i].text,
                  settings_status_str(st), line);
            CHECK(memcmp(&s, &d, sizeof s) == 0, "%schanged something", bad[i].text);
        }
    }
    {
        const char *mixed = "volume = 3\nvolume = 5\nram = 8\nperf = on\n";
        CHECK(parse(&s, mixed, &line) == SET_DUPLICATE && line == 2, "first problem: line %u", line);
        CHECK(s.volume == 3u && s.ram_k == 64u && s.perf,
              "good lines apply around the bad ones");
    }
    {
        /* A value refused does not count as given, so a later line may
         * still set it. */
        CHECK(parse(&s, "ram = 2\nram = 16\n", &line) == SET_BAD_VALUE && line == 1, "retry");
        CHECK(s.ram_k == 16u, "the good line applies");
    }
    {
        char text[COCO_PATH_MAX + 16];
        memcpy(text, "boot_tape = ", 12);
        memset(text + 12, 'a', COCO_PATH_MAX);
        text[12 + COCO_PATH_MAX] = 0;
        CHECK(parse(&s, text, &line) == SET_TOO_LONG && !s.boot_tape[0],
              "path longer than COCO_PATH_MAX");

        char huge[400];
        memset(huge, 'x', sizeof huge - 1);
        huge[sizeof huge - 1] = 0;
        CHECK(parse(&s, huge, &line) == SET_TOO_LONG && line == 1, "line too long");
    }
    {
        const char nul[] = "ram = 16\0\nvolume = 2\n";
        settings_default(&s);
        CHECK(settings_parse(&s, nul, sizeof nul - 1, &line) == SET_SYNTAX && line == 1, "NUL");
        CHECK(s.ram_k == 64u && s.volume == 2u, "the NUL line is refused, the next applies");
    }

    /* ---- the menu's save edits the file (§14; EL §8.7) ---------------- */
    const char *out;
    size_t out_len;
#define REWRITE(text, set) settings_rewrite(text, strlen(text), set, &out, &out_len)
#define IS(want) (out_len == strlen(want) && memcmp(out, want, out_len) == 0)

    /* Nothing to say: an empty file, or none, stays empty. */
    CHECK(REWRITE("", &d) == SET_OK && out_len == 0, "defaults into an empty file: %zu", out_len);

    /* The user's file, CRLF, with comments, a bad line and a stranger. */
    {
        const char *file =
            "# my PicoCalc\r\n"
            "  ram = 16          # as sold\r\n"
            "volume = 11\r\n"
            "boot_tape =      # none\r\n"
            "frobnicate = yes\r\n"
            "\r\n"
            "basic = color\r\n";
        settings_default(&s);
        CHECK(settings_parse(&s, file, strlen(file), &line) == SET_BAD_VALUE && line == 3, "setup");
        /* What the menu changed: the RAM, the volume and the tape. */
        s.ram_k = 64u;
        s.volume = 5u;
        strcpy(s.boot_tape, "TAPE01.cas");
        CHECK(REWRITE(file, &s) == SET_OK, "rewrites");
        const char *want =
            "# my PicoCalc\r\n"
            "  ram = 64          # as sold\r\n"
            "volume = 5\r\n"
            "boot_tape = TAPE01.cas # none\r\n"
            "frobnicate = yes\r\n"
            "\r\n"
            "basic = color\r\n";
        CHECK(IS(want), "the user's file, edited:\n%.*s\nwanted:\n%s", (int)out_len, out, want);

        /* Saving again changes nothing more. */
        static char again[COCO_SETTINGS_FILE_MAX];
        memcpy(again, out, out_len);
        size_t again_len = out_len;
        CHECK(settings_rewrite(again, again_len, &s, &out, &out_len) == SET_OK &&
              out_len == again_len && memcmp(out, again, out_len) == 0, "a second save is the same");
    }

    /* The cartridge in place beside its comment, taken out, or added to a
     * file that had none. */
    {
        const char *file = "cart = edtasm.ccc   # the assembler\nram = 64\n";
        settings_default(&s);
        CHECK(settings_parse(&s, file, strlen(file), &line) == SET_OK, "cart setup");
        strcpy(s.cart, "doodle.ccc");
        CHECK(REWRITE(file, &s) == SET_OK &&
                  IS("cart = doodle.ccc   # the assembler\nram = 64\n"),
              "a new cartridge in place:\n%.*s", (int)out_len, out);
        s.cart[0] = 0;
        CHECK(REWRITE(file, &s) == SET_OK &&
                  IS("cart =              # the assembler\nram = 64\n"),
              "the cartridge taken out:\n%.*s", (int)out_len, out);
        const char *none = "ram = 64 # mine\n";
        settings_default(&s);
        strcpy(s.cart, "/coco/carts/poltergeist.ccc");
        CHECK(REWRITE(none, &s) == SET_OK && out_len > strlen(none) &&
                  memcmp(out, none, strlen(none)) == 0 &&
                  strstr(out, "cart = /coco/carts/poltergeist.ccc\n"),
              "a cartridge the file lacked is added after the user's lines:\n%.*s", (int)out_len,
              out);
    }

    /* status and backlight, as the siblings have them: a backlight of 0
     * is "leave it", which a save keeps as the file has it. */
    {
        const char *file = "backlight = 9 # mine\nstatus = off\n";
        CHECK(settings_parse(&s, file, strlen(file), &line) == SET_OK && s.backlight == 9u &&
                  !s.status, "status and backlight read");
        CHECK(parse(&s, "backlight = 16\n", &line) == SET_BAD_VALUE &&
                  parse(&s, "backlight = 0\n", &line) == SET_BAD_VALUE, "backlight is 1-15");
        settings_default(&s);
        s.status = false;
        s.backlight = 0u;
        CHECK(REWRITE(file, &s) == SET_OK && IS(file), "backlight 0 keeps the file's:\n%.*s",
              (int)out_len, out);
        s.backlight = 12u;
        CHECK(REWRITE(file, &s) == SET_OK && IS("backlight = 12 # mine\nstatus = off\n"),
              "a new backlight in place:\n%.*s", (int)out_len, out);
        settings_default(&s);
        s.backlight = 4u;
        CHECK(REWRITE("", &s) == SET_OK && IS("backlight = 4\n"),
              "a backlight the file lacked is added:\n%.*s", (int)out_len, out);
        settings_default(&s);
        CHECK(REWRITE("backlight = 16 # too bright\n", &s) == SET_OK &&
                  IS("# backlight = 16 # too bright\n"),
              "a refused backlight, none set, is made a comment:\n%.*s", (int)out_len, out);
        settings_default(&s);
        CHECK(settings_parse(&s, out, out_len, &line) == SET_OK && line == 0,
              "and reads back without a problem: line %u", line);
        settings_default(&s);
        s.backlight = 5u;
        CHECK(REWRITE("backlight = 16\n", &s) == SET_OK && IS("backlight = 5\n"),
              "a refused backlight, one set, is replaced:\n%.*s", (int)out_len, out);
    }

    /* A refused value is replaced where it stands, even by the default;
     * when another line gives the key a good value, the refused line
     * becomes a comment instead. */
    {
        settings_default(&s);
        CHECK(REWRITE("ram = 8\nvolume = 9\n", &s) == SET_OK &&
              IS("ram = 64\nvolume = 8\n"), "refused, replaced: %.*s", (int)out_len, out);
        settings_default(&s);
        CHECK(settings_parse(&s, out, out_len, &line) == SET_OK && line == 0,
              "and read back without a problem");
        settings_default(&s);
        s.volume = 3u;
        CHECK(REWRITE("ram = 64\nvolume = 9\nvolume = 2\n", &s) == SET_OK &&
              IS("ram = 64\n# volume = 9\nvolume = 3\n"),
              "refused, but another line is the key's: %.*s", (int)out_len, out);
    }

    /* A value that already says the same stays as the user wrote it,
     * however it is spelt. */
    {
        const char *file = "BASIC = Color\nboot_tape = /coco/tapes/Pipes.CAS\n"
                           "cart = /COCO/carts/Doodle.ccc\n";
        CHECK(parse(&s, file, &line) == SET_OK, "setup");
        strcpy(s.boot_tape, "pipes.cas");
        strcpy(s.cart, "doodle.CCC");
        CHECK(REWRITE(file, &s) == SET_OK && IS(file), "unchanged: %.*s", (int)out_len, out);
    }

    /* Appended lines: only what differs from the default, with the
     * file's line ending, after a last line that had none. */
    {
        settings_default(&s);
        s.extended = false;
        s.ram_k = 4u;
        s.perf = true;
        CHECK(REWRITE("# mine\r\nvolume = 8", &s) == SET_OK &&
              IS("# mine\r\nvolume = 8\r\nbasic = color\r\nram = 4\r\nperf = on\r\n"),
              "appended: %.*s", (int)out_len, out);
        s.extended = true;
        s.ram_k = 64u;
        CHECK(REWRITE("ram = 16\nbasic = color\n", &s) == SET_OK &&
              strncmp(out, "ram = 64\nbasic = extended\n", 26) == 0,
              "back to the default is written where the file has it");
    }

    /* Three columns: a value put in and one changed leave the comments
     * where they were. */
    {
        const char *file =
            "ram       = 16         # 4, 16, 32 or 64\n"
            "boot_tape =            # a file in /coco/tapes/\n"
            "volume\t= 8\t# 0-8\n";
        CHECK(parse(&s, file, &line) == SET_OK, "setup");
        s.ram_k = 32u;
        strcpy(s.boot_tape, "TAPE01.cas");
        s.volume = 3u;
        const char *want =
            "ram       = 32         # 4, 16, 32 or 64\n"
            "boot_tape = TAPE01.cas # a file in /coco/tapes/\n"
            "volume\t= 3\t# 0-8\n";
        CHECK(REWRITE(file, &s) == SET_OK && IS(want), "columns kept:\n%.*s\nwanted:\n%s",
              (int)out_len, out, want);
        strcpy(s.boot_tape, "A-MUCH-LONGER-NAME.cas");
        CHECK(REWRITE(file, &s) == SET_OK &&
              strstr(out, "boot_tape = A-MUCH-LONGER-NAME.cas # a file"),
              "a value too long for the column pushes its comment one space on:\n%.*s",
              (int)out_len, out);
    }

    /* Refusals leave nothing to write. */
    settings_default(&s);
    s.volume = 2u;
    CHECK(REWRITE("volume = 3\nram = 16\nvolume = 4\n", &s) == SET_DUPLICATE,
          "a key given twice refuses the save");
    CHECK(REWRITE("volume = 11\nvolume = 4\n", &s) == SET_OK &&
          IS("# volume = 11\nvolume = 2\n"),
          "a refused line beside the key's own becomes a comment: %.*s", (int)out_len, out);
    {
        static char big[COCO_SETTINGS_FILE_MAX + 1];
        memset(big, '#', COCO_SETTINGS_FILE_MAX - 4u);
        big[COCO_SETTINGS_FILE_MAX - 4u] = '\n';
        big[COCO_SETTINGS_FILE_MAX - 3u] = 0;
        CHECK(REWRITE(big, &s) == SET_TOO_LONG, "a result too long for the file refuses");
    }
    strcpy(s.boot_tape, "/coco/tapes/a #1.cas");
    CHECK(REWRITE("", &s) == SET_MISMATCH, "a name the file cannot hold does not read back");

    /* What the port saves a path as. */
    {
        char n[COCO_PATH_MAX];
        settings_card_name(SETTINGS_TAPE_DIR, "/coco/tapes/x.cas", n);
        CHECK(strcmp(n, "x.cas") == 0, "bare: %s", n);
        settings_card_name(SETTINGS_TAPE_DIR, "/coco/tapes/sub/x.cas", n);
        CHECK(strcmp(n, "/coco/tapes/sub/x.cas") == 0, "a subfolder keeps its path: %s", n);
        settings_card_name(SETTINGS_TAPE_DIR, "/coco/snaps/x.cas", n);
        CHECK(strcmp(n, "/coco/snaps/x.cas") == 0, "another folder keeps its path: %s", n);
        settings_card_name(SETTINGS_CART_DIR, "/coco/carts/edtasm.ccc", n);
        CHECK(strcmp(n, "edtasm.ccc") == 0, "a cartridge, bare: %s", n);
        settings_card_name(SETTINGS_TAPE_DIR, "", n);
        CHECK(n[0] == 0, "none is none");
    }

    TEST_DONE();
}
