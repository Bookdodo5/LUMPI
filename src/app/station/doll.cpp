// THE LUMPI DOLL FIRMWARE (sensor node). Upload env: doll (doll 1) or doll_2 (doll 2).
// Wiring: config/settings.h (doll_pins). Secrets: config/private.h (never in git).
// Hardware & HTTP plumbing: doll_parts.h. Pure rules: src/common/doll_logic.h.
//
// THE STORY, IN ORDER:
//   1. Book on mat + RFID tag -> M_READY; knob turns pages 1..999 (30 s off -> M_IDLE, clears counts).
//   2. Hold RECORD -> M_RECORDING (streams 8192 B chunks, max 10 s); release -> M_CHECKING -> grade.
//   3. Command table (1-8) drives OLED (with IPA), arm/tongue (<= 3 s), phrase + word clips.
//   4. 5 misses in a row -> "Call your teacher? Hold STOP"; 2 s STOP hold calls /v1/help.
//   5. Short STOP toggles M_QUIET (noise report every 1 s -> nest may answer command 5).
//   6. Dark room (nest BH1750) -> M_DARK (blocks everything, cancels any active recording).
//
// FREERTOS TASKS (E1):
//   capture_task   core 1, prio 3 (highest): late I2S reads lose microphone audio
//   control_task   core 1, prio 2: debounced button ISRs (E2), session, counts, mode machine
//   net_task       core 0, prio 2: blocking HTTP stays on Wi-Fi's core, never stalls capture
//   feedback_task  core 1, prio 1 (lowest): OLED, servos, speaker only act on finished jobs
#include <Arduino.h>

#include "doll_logic.h"
#include "doll_parts.h"
#include "messages.h"
#include "session.h"
#include "settings.h"

using namespace lumpi;

// ---- Mode, shared state, and queues ------------------------------------------------------

enum Mode : uint8_t { M_IDLE, M_READY, M_RECORDING, M_CHECKING, M_QUIET, M_DARK };
enum NetKind : uint8_t { NET_START, NET_PRESENCE, NET_BOOK, NET_NOISE, NET_HELP, NET_CLIP };
enum NetEvt : uint8_t { EVT_DARK_ON, EVT_DARK_OFF, EVT_BOOK, EVT_GRADE, EVT_CANCEL, EVT_SHH, EVT_COMING };

struct NetJob { NetKind kind; uint32_t arg; float db; };
struct FbJob { uint32_t command; bool log_csv; char l1[24], l2[24], l3[24]; };

constexpr size_t chunk_bytes = Session::max_chunk_bytes;  // 8192 B = 256 ms
int pool_blocks = 0;
uint8_t* audio_pool = nullptr;
QueueHandle_t free_slots, audio_queue, net_queue, evt_queue, fb_queue, clip_queue;

volatile Mode mode = M_IDLE;
volatile bool capture_active = false, rec_cancel = false, rec_pool_hit = false, help_polling = false;
volatile uint32_t last_held_ms = 0, last_release_ms = 0;

char cur_uid[21] = "04A1B2C3", cur_title[24] = "";
uint32_t cur_attempt = 0, cur_passage = 1, cur_fsr = 0;
bool book_on = false, want_quiet = false;
doll_logic::AttemptReply latest_reply;
doll_parts::ReadingStats latest_stats;

void send_net(NetKind k, uint32_t arg = 0, float db = 0) {
    const NetJob j = {k, arg, db};
    xQueueSend(net_queue, &j, 0);
}

void post_evt(NetEvt e) { xQueueSend(evt_queue, &e, 0); }

void show_screen(const char* l1, const char* l2 = "", const char* l3 = "") {
    FbJob j = {};
    doll_logic::store(j.l1, sizeof(j.l1), l1);
    doll_logic::store(j.l2, sizeof(j.l2), l2);
    doll_logic::store(j.l3, sizeof(j.l3), l3);
    xQueueSend(fb_queue, &j, 0);
}

void run_cmd(uint32_t cmd, bool log_csv = false) {
    FbJob j = {};
    j.command = cmd;
    j.log_csv = log_csv;
    xQueueSend(fb_queue, &j, pdMS_TO_TICKS(100));
}

// ---- Command table (commands 1..8, spec 5-6) ---------------------------------------------

struct CmdAction { const char* l1; const char* l2; const char* wav; };

const CmdAction actions[] = {
    {},
    {"Well done!",         "",               "/phrases/well_done.wav"},
    {"Try that word",      "again",          "/phrases/try_word.wav"},
    {"Please move closer", "and read again", "/phrases/move_closer.wav"},
    {"Let us try again",   "",               "/phrases/try_again.wav"},
    {"Shh",                "",               "/phrases/shh.wav"},
    {"Too dark: please",   "turn on light",  "/phrases/too_dark.wav"},
    {"I have told",        "your teacher",   "/phrases/told_teacher.wav"},
    {"Your teacher",       "is coming",      "/phrases/teacher_coming.wav"},
};

void act_command(const FbJob& job) {
    doll_parts::first_servo_pulse_ms = 0;
    if (job.command == retry_word && latest_reply.words_n > 0) {
        for (int i = 0; i < latest_reply.words_n; ++i) {
            const doll_logic::WordLine& w = latest_reply.words[i];
            if (w.right) {
                continue;
            }
            doll_parts::oled_show_word(w.word, w.ipa, w.heard);
            doll_parts::run_gesture(retry_word);
            doll_parts::play_wav_file(actions[retry_word].wav);
            if (w.has_clip) {
                send_net(NET_CLIP, w.page_index);
                doll_parts::ClipSlice s = {};
                while (xQueueReceive(clip_queue, &s, pdMS_TO_TICKS(6000)) == pdTRUE && s.bytes > 0) {
                    doll_parts::speaker_write_mono(s.samples, s.bytes / 2);
                    esp_task_wdt_reset();
                }
                if (doll_parts::speaker_ok) {
                    i2s_zero_dma_buffer(I2S_NUM_1);
                }
            }
            doll_parts::wait_ms(1200);
        }
    } else if (job.command >= 1 && job.command <= 8) {
        doll_parts::oled_show("LUMPI", actions[job.command].l1, actions[job.command].l2);
        doll_parts::run_gesture(job.command);
        doll_parts::play_wav_file(actions[job.command].wav);
    }
    doll_parts::servos_off();
    if (job.log_csv) {
        const uint32_t pulse_ms = doll_parts::first_servo_pulse_ms
                                      ? doll_parts::first_servo_pulse_ms - latest_stats.release_ms
                                      : millis() - latest_stats.release_ms;
        doll_parts::csv_reading(latest_stats, job.command, pulse_ms);
    }
}

// ---- Task 1: capture_task (Core 1, Priority 3 = highest) ---------------------------------

void capture_task(void*) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        if (mode == M_RECORDING) {
            capture_active = true;
            rec_pool_hit = false;
            if (doll_parts::mic_ok) {
                i2s_zero_dma_buffer(I2S_NUM_0);
            }
            size_t total = 0;
            while (mode == M_RECORDING && total < Session::max_bytes) {
                int slot = -1;
                if (xQueueReceive(free_slots, &slot, pdMS_TO_TICKS(150)) != pdTRUE) {
                    rec_pool_hit = true;  // Wi-Fi stalled: end cleanly at last whole block
                    break;
                }
                int16_t* dst = reinterpret_cast<int16_t*>(audio_pool + static_cast<size_t>(slot) * chunk_bytes);
                size_t n = 0;
                const size_t cap = chunk_bytes / 2;
                while (mode == M_RECORDING && n < cap && total + n * 2 < Session::max_bytes) {
                    const size_t want = min<size_t>(256, min(cap - n, (Session::max_bytes - total) / 2 - n));
                    n += doll_parts::mic_read_pcm16(dst + n, want);
                    esp_task_wdt_reset();
                }
                if (n == 0) {
                    xQueueSend(free_slots, &slot, portMAX_DELAY);
                    break;
                }
                total += n * 2;
                const doll_parts::AudioBlock b = {slot, n * 2, false};
                xQueueSend(audio_queue, &b, portMAX_DELAY);
            }
            const doll_parts::AudioBlock end = {-1, 0, true};
            xQueueSend(audio_queue, &end, portMAX_DELAY);
            capture_active = false;
            continue;
        }
        if (mode == M_QUIET) {
            uint32_t peaks[4] = {};
            int i = 0;
            for (; i < 4 && mode == M_QUIET; ++i) {
                peaks[i] = doll_parts::mic_peak_250ms();
                esp_task_wdt_reset();
            }
            if (i == 4 && mode == M_QUIET) {
                const uint32_t p = doll_logic::quietest_of_four(peaks);
                send_net(NET_NOISE, p, doll_logic::peak_to_db(p));
            }
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ---- Task 2: net_task (Core 0, Priority 2 = beside Wi-Fi) --------------------------------

void drain_audio() {
    const uint32_t t0 = millis();
    while (millis() - t0 < 1500) {
        doll_parts::AudioBlock b = {};
        if (xQueueReceive(audio_queue, &b, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (b.slot >= 0) {
                xQueueSend(free_slots, &b.slot, portMAX_DELAY);
            }
            if (b.final_block) {
                return;
            }
        } else if (!capture_active) {
            return;
        }
    }
}

void upload_reading(uint32_t& last_presence_ms) {
    latest_stats = {};
    latest_stats.attempt = cur_attempt;
    latest_reply = {};
    latest_reply.valid = true;
    latest_reply.code = uncertain;

    String ans;
    const String start_q = "&attempt=" + String(cur_attempt) + "&passage=" + String(cur_passage) +
                           "&uid=" + String(cur_uid) + "&fsr=" + String(cur_fsr);
    const int code = doll_parts::tell_nest("POST", route::start, start_q, ans);
    if (code != 200) {
        if (mode == M_RECORDING) {
            mode = M_CHECKING;
        }
        drain_audio();
        if (code == 409 && ans.indexOf("dark") >= 0) {
            post_evt(EVT_DARK_ON);
            post_evt(EVT_CANCEL);
        } else {
            post_evt(EVT_GRADE);
        }
        return;
    }

    uint32_t bytes = 0;
    bool ok = true, saw_dark = false;
    for (;;) {
        esp_task_wdt_reset();
        if (millis() - last_presence_ms >= 3000) {
            last_presence_ms = millis();
            if (doll_parts::send_presence(book_on, cur_fsr, want_quiet) == 1) {
                saw_dark = rec_cancel = true;
                mode = M_CHECKING;
            }
        }
        doll_parts::AudioBlock b = {};
        if (xQueueReceive(audio_queue, &b, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }
        if (b.final_block) {
            break;
        }
        if (rec_cancel || !ok) {
            xQueueSend(free_slots, &b.slot, portMAX_DELAY);
            continue;
        }
        const uint8_t* data = audio_pool + static_cast<size_t>(b.slot) * chunk_bytes;
        const bool acked = doll_parts::send_chunk(cur_attempt, latest_stats.chunks, data, b.bytes,
                                                  latest_stats.chunk_tries);
        xQueueSend(free_slots, &b.slot, portMAX_DELAY);
        if (!acked) {
            ok = false;
            mode = M_CHECKING;
        } else {
            latest_stats.chunks += 1;
            bytes += b.bytes;
        }
    }

    const bool too_short = !rec_pool_hit && last_held_ms < doll_tuning::min_record_ms;
    if (saw_dark || rec_cancel || latest_stats.chunks == 0 || too_short || !ok) {
        doll_parts::tell_nest("POST", route::cancel, "&attempt=" + String(cur_attempt), ans, nullptr, 0, 2000);
        if (saw_dark) {
            post_evt(EVT_DARK_ON);
        }
        post_evt(ok ? EVT_CANCEL : EVT_GRADE);
        return;
    }

    const String fin_q = "&attempt=" + String(cur_attempt) + "&chunks=" + String(latest_stats.chunks) +
                         "&bytes=" + String(bytes);
    const int fin = doll_parts::tell_nest("POST", route::finish, fin_q, ans);
    const uint32_t fin_ms = millis();
    latest_stats.finish_ms = fin_ms - last_release_ms;
    if (fin == 200) {
        doll_parts::poll_attempt(cur_attempt, latest_reply);
    }
    latest_stats.grade_ms = millis() - fin_ms;
    post_evt(EVT_GRADE);
}

void net_task(void*) {
    esp_task_wdt_add(nullptr);
    uint32_t last_presence_ms = 0, last_help_ms = 0;

    auto do_presence = [&]() {
        last_presence_ms = millis();
        const int d = doll_parts::send_presence(book_on, cur_fsr, want_quiet);
        if (d >= 0) {
            post_evt(d == 1 ? EVT_DARK_ON : EVT_DARK_OFF);
        }
    };

    auto do_help = [&]() {
        last_help_ms = millis();
        String ans;
        if (doll_parts::tell_nest("GET", route::help, "", ans, nullptr, 0, 2000) == 200) {
            if (ans == "dark") {
                post_evt(EVT_DARK_ON);
            } else if (ans == "coming") {
                post_evt(EVT_COMING);
            }
        }
    };

    for (;;) {
        esp_task_wdt_reset();
        doll_parts::wifi_maintain(mode != M_RECORDING && mode != M_CHECKING);
        if (!lumpi_net::connected()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        NetJob job = {};
        if (xQueueReceive(net_queue, &job, pdMS_TO_TICKS(50)) == pdTRUE) {
            String ans;
            if (job.kind == NET_START) {
                upload_reading(last_presence_ms);
            } else if (job.kind == NET_PRESENCE) {
                do_presence();
            } else if (job.kind == NET_HELP) {
                do_help();
            } else if (job.kind == NET_BOOK) {
                const String q = "&uid=" + String(cur_uid) + "&passage=" + String(cur_passage);
                if (doll_parts::tell_nest("GET", route::book, q, ans, nullptr, 0, 3000) == 200 &&
                    ans.startsWith("ok,")) {
                    doll_logic::store(cur_title, sizeof(cur_title), ans.substring(3).c_str());
                    post_evt(EVT_BOOK);
                }
            } else if (job.kind == NET_NOISE) {
                const String q = "&db=" + String(job.db, 1) + "&peak=" + String(job.arg);
                if (doll_parts::tell_nest("POST", route::noise, q, ans, nullptr, 0, 2000) == 200) {
                    if (ans == "dark") {
                        post_evt(EVT_DARK_ON);
                    } else if (ans == "command,5") {
                        post_evt(EVT_SHH);
                    }
                }
            } else if (job.kind == NET_CLIP) {
                doll_parts::stream_word_clip(cur_attempt, job.arg, clip_queue);
            }
        }
        if (millis() - last_presence_ms >= 3000) {
            do_presence();
        }
        if (help_polling && millis() - last_help_ms >= 3000) {
            do_help();
        }
    }
}

// ---- Task 3: feedback_task (Core 1, Priority 1 = lowest) ---------------------------------

void feedback_task(void*) {
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        doll_parts::servos_check_timeout();
        FbJob j = {};
        if (xQueueReceive(fb_queue, &j, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (j.command == 0) {
                doll_parts::oled_show(j.l1, j.l2, j.l3);
            } else {
                act_command(j);
            }
        }
    }
}

// ---- Task 4: control_task (Core 1, Priority 2 = mode machine) ----------------------------

void control_task(void*) {
    esp_task_wdt_add(nullptr);
    doll_logic::MistakeCounts mistakes;
    doll_logic::PageKnob knob;
    knob.reset(doll_parts::read_adc(doll_pins::page_knob));
    doll_parts::Debouncer rec_btn, stop_btn;

    bool proposal = false, stop_held = false, stop_fired = false;
    uint32_t lift_ms = 0, rec_ms = 0, timer_ms = 0, prop_ms = 0, stop_ms = 0, sens_ms = 0, rfid_ms = 0;

    auto idle_mode = [&]() -> Mode { return want_quiet ? M_QUIET : (book_on ? M_READY : M_IDLE); };

    auto draw_mode = [&]() {
        if (mode == M_DARK) {
            show_screen("Too dark:", "please turn on", "the light");
        } else if (proposal) {
            show_screen("Call your teacher?", "Hold STOP (2 s)", "or press RECORD");
        } else if (mode == M_QUIET) {
            show_screen("Quiet mode", "Listening for noise", "STOP = leave");
        } else if (mode == M_READY) {
            char page[16];
            snprintf(page, sizeof(page), "Page %d", knob.page);
            show_screen(cur_title[0] ? cur_title : cur_uid, page, "Hold RECORD to read");
        } else if (mode == M_IDLE) {
            show_screen("LUMPI", "Put a book on", "the mat");
        }
    };

    auto stop_rec = [&](bool force_cancel) {
        const uint32_t now = millis();
        last_release_ms = now;
        last_held_ms = now - rec_ms;
        rec_cancel = force_cancel || (last_held_ms < doll_tuning::min_record_ms && !rec_pool_hit);
        mode = rec_cancel ? idle_mode() : M_CHECKING;
        if (rec_cancel) {
            draw_mode();
        } else {
            show_screen("Checking...", "Please wait");
        }
    };

    draw_mode();

    for (;;) {
        esp_task_wdt_reset();
        const uint32_t now = millis();

        // 1. Button interrupts -> debouncers (E2).
        doll_parts::ButtonEdge e = {};
        while (xQueueReceive(doll_parts::button_queue, &e, 0) == pdTRUE) {
            (e.pin == doll_pins::record ? rec_btn : stop_btn).on_edge(e.level, e.at_ms);
        }
        uint8_t lv = HIGH;
        if (rec_btn.ready(now, lv)) {
            if (lv == LOW) {
                if (proposal) {
                    proposal = false;
                    mistakes.reset_reached();  // reading on restarts the 5-miss count
                }
                if (mode == M_READY) {
                    cur_attempt += 1;
                    cur_passage = knob.page;
                    cur_fsr = doll_parts::read_adc(doll_pins::fsr);
                    help_polling = rec_cancel = false;
                    rec_ms = now;
                    timer_ms = 0;
                    mode = M_RECORDING;
                    send_net(NET_START);
                }
            } else if (mode == M_RECORDING) {
                stop_rec(false);
            }
        }
        if (stop_btn.ready(now, lv)) {
            if (lv == LOW && mode != M_RECORDING && mode != M_DARK) {
                stop_held = true;
                stop_fired = false;
                stop_ms = now;
            } else if (lv == HIGH && stop_held) {
                stop_held = false;
                if (!stop_fired && now - stop_ms < doll_tuning::stop_hold_ms &&
                    (mode == M_IDLE || mode == M_READY || mode == M_QUIET)) {
                    want_quiet = !want_quiet;
                    mode = idle_mode();
                    send_net(NET_PRESENCE);
                    draw_mode();
                }
            }
        }
        if (stop_held && !stop_fired && proposal && mode != M_DARK && now - stop_ms >= doll_tuning::stop_hold_ms) {
            stop_fired = true;
            proposal = false;
            mistakes.reset_reached();
            help_polling = true;
            send_net(NET_HELP);
            run_cmd(told_teacher);
        }

        // 2. Recording timer (10 s cap) and proposal timeout (30 s).
        if (mode == M_RECORDING) {
            if (now - rec_ms >= doll_tuning::max_record_ms || rec_pool_hit) {
                stop_rec(false);
            } else if (now - timer_ms >= 200) {
                timer_ms = now;
                char line[20];
                snprintf(line, sizeof(line), "%.1f / 10 s", (now - rec_ms) / 1000.0f);
                show_screen("Recording...", line);
            }
        }
        if (proposal && now - prop_ms >= doll_tuning::proposal_timeout_ms) {
            proposal = false;
            draw_mode();
        }

        // 3. Events from net_task.
        NetEvt ev;
        while (xQueueReceive(evt_queue, &ev, 0) == pdTRUE) {
            if (ev == EVT_DARK_ON && mode != M_DARK) {
                if (mode == M_RECORDING) {
                    stop_rec(true);
                }
                mode = M_DARK;
                proposal = false;
                doll_parts::csv_dark(true);
                run_cmd(too_dark);
            } else if (ev == EVT_DARK_OFF && mode == M_DARK) {
                mode = idle_mode();
                doll_parts::csv_dark(false);
                draw_mode();
            } else if (ev == EVT_BOOK && mode == M_READY) {
                draw_mode();
            } else if (ev == EVT_CANCEL && mode != M_DARK) {
                mode = idle_mode();
                draw_mode();
            } else if (ev == EVT_GRADE) {
                if (mode == M_RECORDING) {
                    last_release_ms = now;
                    last_held_ms = now - rec_ms;
                }
                if (latest_reply.code == too_dark) {
                    if (mode != M_DARK) {
                        mode = M_DARK;
                        doll_parts::csv_dark(true);
                    }
                    run_cmd(too_dark);
                    continue;
                }
                mode = idle_mode();
                latest_stats.held_ms = last_held_ms;
                latest_stats.release_ms = last_release_ms;
                for (int i = 0; i < latest_reply.words_n; ++i) {
                    if (!latest_reply.words[i].right) {
                        latest_stats.wrong_words += 1;
                    }
                }
                const bool ask = mistakes.apply(latest_reply, doll_parts::csv_count);
                run_cmd(latest_reply.code, true);
                if (ask && mode != M_DARK) {
                    proposal = true;
                    prop_ms = millis();
                    show_screen("Call your teacher?", "Hold STOP (2 s)", "or press RECORD");
                }
            } else if (ev == EVT_SHH && mode == M_QUIET) {
                run_cmd(too_noisy);
            } else if (ev == EVT_COMING) {
                help_polling = false;
                run_cmd(teacher_coming);
            }
        }

        // 4. FSR mat (30 s lift-off grace), page knob, and RFID tag.
        if (now - sens_ms >= 50) {
            sens_ms = now;
            cur_fsr = doll_parts::read_adc(doll_pins::fsr);
            if (static_cast<int>(cur_fsr) >= doll_tuning::book_on_fsr) {
                lift_ms = 0;
                if (!book_on) {
                    book_on = true;
                    send_net(NET_PRESENCE);
                    send_net(NET_BOOK);
                    if (mode == M_IDLE) {
                        mode = M_READY;
                        draw_mode();
                    }
                }
            } else if (book_on) {
                if (lift_ms == 0) {
                    lift_ms = now;
                } else if (now - lift_ms >= doll_tuning::lift_grace_ms) {
                    book_on = proposal = false;
                    lift_ms = 0;
                    mistakes.clear();
                    send_net(NET_PRESENCE);
                    if (mode == M_READY) {
                        mode = M_IDLE;
                        draw_mode();
                    }
                }
            }
            if (mode == M_READY &&
                knob.step(doll_parts::read_adc(doll_pins::page_knob), doll_tuning::knob_step, Session::max_passage)) {
                cur_passage = knob.page;
                draw_mode();
            }
        }
        if (mode != M_RECORDING && now - rfid_ms >= 250) {
            rfid_ms = now;
            char seen[21] = {};
            if (doll_parts::read_rfid(seen, sizeof(seen)) && strcmp(seen, cur_uid) != 0) {
                doll_logic::store(cur_uid, sizeof(cur_uid), seen);
                cur_title[0] = '\0';
                knob.reset(doll_parts::read_adc(doll_pins::page_knob));
                cur_passage = 1;
                send_net(NET_BOOK);
                if (mode == M_READY) {
                    draw_mode();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---- Boot and 60 s heartbeat -------------------------------------------------------------

void setup() {
    doll_parts::servos_init();  // first: servos always boot limp
    Serial.begin(115200);
    delay(400);

    pool_blocks = ESP.getFreeHeap() > 180000 ? 10 : 6;
    audio_pool = static_cast<uint8_t*>(malloc(static_cast<size_t>(pool_blocks) * chunk_bytes));
    if (!audio_pool) {
        pool_blocks = 4;
        audio_pool = static_cast<uint8_t*>(malloc(static_cast<size_t>(pool_blocks) * chunk_bytes));
    }
    if (!audio_pool) {
        Serial.println("[memory] audio pool failed");
        while (true) {
            delay(1000);
        }
    }

    doll_parts::sensors_init();
    doll_parts::oled_init();
    doll_parts::audio_init();

    Serial.printf("[boot] doll %d | pool %dx8kB | mic=%d spk=%d fs=%d rfid=%d oled=%d | key=%u chars\n",
                  LUMPI_NODE_ID, pool_blocks, doll_parts::mic_ok, doll_parts::speaker_ok, doll_parts::fs_ok,
                  doll_parts::rfid_ok, doll_parts::oled_ok, static_cast<unsigned>(strlen(LUMPI_LAN_KEY)));
    doll_parts::csv_boot();
    lumpi_net::begin();

    doll_parts::button_queue = xQueueCreate(16, sizeof(doll_parts::ButtonEdge));
    free_slots = xQueueCreate(pool_blocks, sizeof(int));
    audio_queue = xQueueCreate(pool_blocks + 2, sizeof(doll_parts::AudioBlock));
    net_queue = xQueueCreate(8, sizeof(NetJob));
    evt_queue = xQueueCreate(8, sizeof(NetEvt));
    fb_queue = xQueueCreate(8, sizeof(FbJob));
    clip_queue = xQueueCreate(4, sizeof(doll_parts::ClipSlice));
    for (int i = 0; i < pool_blocks; ++i) {
        xQueueSend(free_slots, &i, 0);
    }

    esp_task_wdt_init(30, true);
    xTaskCreatePinnedToCore(capture_task, "capture", 4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(control_task, "control", 6144, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(net_task, "net", 8192, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(feedback_task, "feedback", 4096, nullptr, 1, nullptr, 1);
    doll_parts::buttons_init();  // attach CHANGE ISRs last, after queues exist
}

void loop() {
    static uint32_t last_beat_ms = 0;
    if (millis() - last_beat_ms >= 60000) {
        last_beat_ms = millis();
        doll_parts::csv_beat();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
}
