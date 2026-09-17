#include "servo_emotion.h"

#include "boards/board.h"

#if BOARD_SERVO_EMOTION_RUNTIME_ENABLE

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define SERVO_PERIOD_US 20000
#define SERVO_TICK_US 50000
#define SERVO_GESTURE_HOLD_US 1500000
#define SERVO_STEP_US 2
#define SERVO_SPEAK_DELTA_US 10

static const char *TAG = "vimate.servo";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_timer;
static bool s_ready;
static bool s_active;
static bool s_speaking;
static int s_center_us = BOARD_SERVO_EMOTION_NEUTRAL_US;
static int s_current_us = BOARD_SERVO_EMOTION_NEUTRAL_US;
static int64_t s_idle_deadline_us;

static int clamp_pulse(int pulse_us) {
  if (pulse_us < BOARD_SERVO_EMOTION_MIN_US)
    return BOARD_SERVO_EMOTION_MIN_US;
  if (pulse_us > BOARD_SERVO_EMOTION_MAX_US)
    return BOARD_SERVO_EMOTION_MAX_US;
  return pulse_us;
}

static uint32_t pulse_to_duty(int pulse_us) {
  return (uint32_t)(((uint64_t)pulse_us * (1U << LEDC_TIMER_14_BIT)) /
                    SERVO_PERIOD_US);
}

static void write_pulse(int pulse_us) {
  uint32_t duty = pulse_to_duty(clamp_pulse(pulse_us));
  if (ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty) == ESP_OK) {
    (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  }
}

static int emotion_center(vimate_emotion_t emotion) {
  switch (emotion) {
  case EMOTION_HAPPY:
  case EMOTION_LAUGHING:
  case EMOTION_FUNNY:
  case EMOTION_LOVING:
  case EMOTION_WINKING:
  case EMOTION_DELICIOUS:
  case EMOTION_KISSY:
  case EMOTION_CONFIDENT:
  case EMOTION_SILLY:
    return BOARD_SERVO_EMOTION_NEUTRAL_US + 25;
  case EMOTION_SAD:
  case EMOTION_CRYING:
  case EMOTION_EMBARRASSED:
  case EMOTION_SLEEPY:
    return BOARD_SERVO_EMOTION_NEUTRAL_US - 25;
  case EMOTION_ANGRY:
  case EMOTION_SURPRISED:
  case EMOTION_SHOCKED:
  case EMOTION_CONFUSED:
    return BOARD_SERVO_EMOTION_NEUTRAL_US + 15;
  case EMOTION_THINKING:
  case EMOTION_COOL:
  case EMOTION_RELAXED:
  case EMOTION_NEUTRAL:
  default:
    return BOARD_SERVO_EMOTION_NEUTRAL_US;
  }
}

static void servo_tick(void *arg) {
  (void)arg;
  const int64_t now = esp_timer_get_time();
  int desired_us;
  int output_us;
  bool disable_output = false;

  portENTER_CRITICAL(&s_lock);
  if (!s_ready || !s_active) {
    portEXIT_CRITICAL(&s_lock);
    return;
  }

  desired_us = s_center_us;
  if (s_speaking) {
    desired_us +=
        ((now / 250000) & 1) ? SERVO_SPEAK_DELTA_US : -SERVO_SPEAK_DELTA_US;
  } else if (now >= s_idle_deadline_us) {
    desired_us = BOARD_SERVO_EMOTION_NEUTRAL_US;
  }
  desired_us = clamp_pulse(desired_us);

  if (s_current_us < desired_us) {
    s_current_us += SERVO_STEP_US;
    if (s_current_us > desired_us)
      s_current_us = desired_us;
  } else if (s_current_us > desired_us) {
    s_current_us -= SERVO_STEP_US;
    if (s_current_us < desired_us)
      s_current_us = desired_us;
  }
  output_us = s_current_us;

  if (!s_speaking && now >= s_idle_deadline_us &&
      s_current_us == BOARD_SERVO_EMOTION_NEUTRAL_US) {
    s_active = false;
    disable_output = true;
  }
  portEXIT_CRITICAL(&s_lock);

  if (disable_output) {
    (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
  } else {
    write_pulse(output_us);
  }
}

esp_err_t servo_emotion_init(void) {
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_14_BIT,
      .timer_num = LEDC_TIMER_1,
      .freq_hz = 50,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  esp_err_t err = ledc_timer_config(&timer);
  if (err != ESP_OK)
    return err;

  ledc_channel_config_t channel = {
      .gpio_num = BOARD_SERVO_EMOTION_GPIO,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_1,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_1,
      .duty = 0,
      .hpoint = 0,
  };
  err = ledc_channel_config(&channel);
  if (err != ESP_OK)
    return err;

  const esp_timer_create_args_t timer_args = {
      .callback = servo_tick,
      .name = "servo_emo",
  };
  err = esp_timer_create(&timer_args, &s_timer);
  if (err != ESP_OK)
    return err;
  err = esp_timer_start_periodic(s_timer, SERVO_TICK_US);
  if (err != ESP_OK)
    return err;

  portENTER_CRITICAL(&s_lock);
  s_ready = true;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "Emotion servo ready GPIO%d 50Hz pulse=%d..%d neutral=%dus",
           BOARD_SERVO_EMOTION_GPIO, BOARD_SERVO_EMOTION_MIN_US,
           BOARD_SERVO_EMOTION_MAX_US, BOARD_SERVO_EMOTION_NEUTRAL_US);
  return ESP_OK;
}

void servo_emotion_set_emotion(vimate_emotion_t emotion) {
  if (!s_ready)
    return;
  int center_us = emotion_center(emotion);
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  s_center_us = center_us;
  s_idle_deadline_us = now + SERVO_GESTURE_HOLD_US;
  s_active = true;
  portEXIT_CRITICAL(&s_lock);
  ESP_LOGI(TAG, "emotion=%s center=%dus", vimate_emotion_to_str(emotion),
           center_us);
}

void servo_emotion_set_speaking(bool speaking) {
  if (!s_ready)
    return;
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&s_lock);
  s_speaking = speaking;
  if (speaking) {
    s_active = true;
  } else {
    s_center_us = BOARD_SERVO_EMOTION_NEUTRAL_US;
    s_idle_deadline_us = now;
  }
  portEXIT_CRITICAL(&s_lock);
}

void servo_emotion_stop(void) { servo_emotion_set_speaking(false); }

#else

esp_err_t servo_emotion_init(void) { return ESP_OK; }

void servo_emotion_set_emotion(vimate_emotion_t emotion) { (void)emotion; }

void servo_emotion_set_speaking(bool speaking) { (void)speaking; }

void servo_emotion_stop(void) {}

#endif
