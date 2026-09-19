/**
 * @file nimbo.c
 * @brief 云宝 (Nimbo) 表情模块实现
 *
 * 身体：七瓣扇贝波浪边云朵（白色配色），由 8 个圆形对象拼成
 * 五官：深蓝椭圆眼睛 + 白色高光，腮红（半透明粉），嘴巴（弧线/椭圆）
 * 动画：lv_timer 驱动 — 呼吸浮动、眨眼、眼神移动、说话嘴动、睡觉 zzz
 */
#include "nimbo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NIMBO_TICK_MS 20   /* 动画定时器周期 */

/* ==================== 表情参数定义 ==================== */

typedef struct {
    float eye_open;   /* 眼睛开合度 0~1（0=闭合细线，1=全开） */
    float eye_scale;  /* 眼睛整体放大倍数 */
    float look_x;     /* 眼神水平位移 px */
    float look_y;     /* 眼神垂直位移 px */
    int   mouth;      /* 0=微笑弧 1=O型 2=平线 3=倒弧 4=说话开合 */
    int   blush;      /* 腮红不透明度 0~100 */
    int   body_tint;  /* 身体色调 0=白 1=微红(生气) 2=微粉(开心) */
    int   zzz;        /* 睡觉 zzz 粒子 */
    int   bounce;     /* 开心弹跳 */
    const char * name;/* 表情名（演示用） */
} nimbo_emotion_def_t;

static const nimbo_emotion_def_t s_emotions[NIMBO_EMOTION_COUNT] = {
    /* 待机：正常眼睛、左右看、呼吸、偶尔眨眼 */
    { 0.95f, 1.00f, 0,   0, 0,  0, 0, 0, 0, "待机" },
    /* 开心：笑眼眯起、微笑嘴、腮红、轻弹 */
    { 0.78f, 1.00f, 0,  -1, 0, 70, 2, 0, 1, "开心" },
    /* 思考：半闭眼、眼神向上、嘴微偏 */
    { 0.72f, 0.98f, 0,  -4, 2,  0, 0, 0, 0, "思考" },
    /* 聆听：圆睁专注、嘴微开 */
    { 1.00f, 1.15f, 0,   0, 4,  0, 0, 0, 0, "聆听" },
    /* 说话：眼睛正常、嘴开合动画 */
    { 1.00f, 1.00f, 0,   0, 4, 10, 0, 0, 0, "说话" },
    /* 睡觉：闭眼细线、zzz 飘、身体下沉 */
    { 0.06f, 1.00f, 0,   2, 2,  0, 0, 1, 0, "睡觉" },
    /* 生气：眼睛眯窄、倒弧嘴、身体微红 */
    { 0.52f, 0.95f, 0,   0, 3,  0, 1, 0, 0, "生气" },
    /* 惊讶：圆睁大眼、O 嘴、微弹 */
    { 1.00f, 1.35f, 0,  -2, 1, 20, 0, 0, 1, "惊讶" },
};

/* ==================== 颜色 ==================== */
#define CLR_BODY      lv_color_hex(0xFFFFFF)   /* 白色云身 */
#define CLR_BODY_EDGE lv_color_hex(0x2B3550)   /* 云身外轮廓（深蓝灰，与眼睛同色系） */
#define CLR_EYE       lv_color_hex(0x2B3550)   /* 深蓝眼睛 */
#define CLR_BLUSH     lv_color_hex(0xEFA9B8)   /* 粉色腮红 */
#define CLR_MOUTH     lv_color_hex(0x2B3550)   /* 嘴色 */
#define CLR_ANGRY     lv_color_hex(0xF6E0E0)   /* 生气微红 */
#define CLR_HAPPY     lv_color_hex(0xFDF0F0)   /* 开心微粉 */

/* 身体圆布局（240x240 坐标系，云朵中心约 120,140）：
 * 0=中心大圆，1~5=顶部扇贝，6~7=底部加宽（使底部收平） */
static const int s_body_cx[8]  = { 120, 120,  92, 148,  68, 172,  86, 154 };
static const int s_body_cy[8]  = { 140, 102, 112, 112, 132, 132, 162, 162 };
static const int s_body_r[8]   = {  58,  26,  28,  28,  26,  26,  34,  34 };

#define EYE_W  30
#define EYE_H  34
#define EYE_GAP 30        /* 两眼中点间距 */
#define EYE_CY 122        /* 眼中点 y */
#define BLUSH_DX 48       /* 腮红距中心 x */
#define BLUSH_DY 10
#define BLUSH_R 13

#define MOUTH_CY 158      /* 嘴中心 y */
#define MOUTH_W  44

/* ==================== 结构 ==================== */

struct nimbo_t {
    lv_obj_t * root;          /* 云朵整体容器（呼吸/弹跳用） */
    lv_obj_t * body_out[8];   /* 外轮廓层（略大的轮廓色圆，被 body 盖住中心） */
    lv_obj_t * body[8];       /* 身体层（实心无描边，盖在轮廓层上） */
    lv_obj_t * eye_grp;       /* 眼睛组（眼神移动） */
    lv_obj_t * eye_l, * eye_r;
    lv_obj_t * hl_l, * hl_r;
    lv_obj_t * blush_l, * blush_r;
    lv_obj_t * mouth_arc;     /* 微笑/倒弧嘴 */
    lv_obj_t * mouth_obj;     /* O型/平线/说话嘴 */
    lv_obj_t * zzz[3];        /* 睡觉粒子 */

    lv_timer_t * timer;
    int   emotion;
    float t;                  /* 累计时间 s */
    float blink_t;            /* 眨眼倒计时（<0 表示正在眨） */
    float blink_hold;         /* 眨眼进行时 */
    float shot_countdown;     /* 截图倒计时（main 使用） */
};

/* ==================== 静态函数 ==================== */

static lv_obj_t * make_circle(lv_obj_t * parent, int x, int y, int r,
                              lv_color_t color, lv_color_t edge, int border_w)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x - r, y - r);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border_w, 0);
    lv_obj_set_style_border_color(o, edge, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    return o;
}

/* 眨眼：眼睛 scaleY 1 -> 0.08 -> 1，120ms 完成 */
static void apply_blink(nimbo_t * nb, float k)
{
    /* k: 0~1 眨的过程，0.5 为最闭合 */
    float sy;
    if (k < 0.5f)      sy = 1.0f - 0.92f * (k / 0.5f);
    else               sy = 0.08f + 0.92f * ((k - 0.5f) / 0.5f);
    lv_obj_set_style_transform_scale_y(nb->eye_l, (int32_t)(sy * 100), 0);
    lv_obj_set_style_transform_scale_y(nb->eye_r, (int32_t)(sy * 100), 0);
}

/* 眼睛静态开合（非眨眼时的基准） */
static void apply_eye_open(nimbo_t * nb, float open)
{
    if (nb->blink_t >= 0) {
        lv_obj_set_style_transform_scale_y(nb->eye_l, (int32_t)(open * 100), 0);
        lv_obj_set_style_transform_scale_y(nb->eye_r, (int32_t)(open * 100), 0);
    }
}

/* 嘴巴：根据 mouth 类型设置显示 */
static void apply_mouth(nimbo_t * nb, int mouth, float open_k)
{
    /* open_k: 说话时的开合 0~1 */
    lv_obj_t * arc = nb->mouth_arc;
    lv_obj_t * mo  = nb->mouth_obj;

    switch (mouth) {
    case 0: /* 微笑弧：下半弧 */
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_bg_angles(arc, 180, 360);
        lv_obj_add_flag(mo, LV_OBJ_FLAG_HIDDEN);
        break;
    case 3: /* 倒弧（生气）：上半弧 */
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
        lv_arc_set_bg_angles(arc, 0, 180);
        lv_obj_add_flag(mo, LV_OBJ_FLAG_HIDDEN);
        break;
    case 1: /* O 型：椭圆环 */
        lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mo, 18, 20);
        lv_obj_set_style_radius(mo, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(mo, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mo, 3, 0);
        lv_obj_set_style_transform_scale_y(mo, 100, 0);
        break;
    case 2: /* 平线 */
        lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mo, 24, 5);
        lv_obj_set_style_radius(mo, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(mo, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mo, 0, 0);
        lv_obj_set_style_transform_scale_y(mo, 100, 0);
        break;
    case 4: /* 说话：椭圆开合 */
        lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mo, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mo, 22, 22);
        lv_obj_set_style_radius(mo, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(mo, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(mo, 0, 0);
        lv_obj_set_style_transform_scale_y(mo, (int32_t)(20 + 80 * open_k), 0);
        break;
    default:
        break;
    }
}

/* 身体色调 */
static lv_color_t body_color_of(int tint)
{
    if (tint == 1) return CLR_ANGRY;
    if (tint == 2) return CLR_HAPPY;
    return CLR_BODY;
}

/* ==================== 动画驱动 ==================== */

static void nimbo_timer_cb(lv_timer_t * timer)
{
    nimbo_t * nb = (nimbo_t *)lv_timer_get_user_data(timer);
    const nimbo_emotion_def_t * def = &s_emotions[nb->emotion];

    nb->t += NIMBO_TICK_MS / 1000.0f;
    float t = nb->t;

    /* ---- 眨眼 ---- */
    if (nb->blink_t >= 0) {
        nb->blink_t -= NIMBO_TICK_MS / 1000.0f;
        if (nb->blink_t <= 0) {
            nb->blink_t = -1;                     /* 眨完 */
            nb->blink_hold = 2.0f + (float)(rand() % 3000) / 1000.0f; /* 下次 2~5s */
            apply_eye_open(nb, def->eye_open);
        }
        else {
            float k = 1.0f - nb->blink_t / 0.13f; /* 眨眼 130ms */
            apply_blink(nb, k);
        }
    }
    else {
        nb->blink_hold -= NIMBO_TICK_MS / 1000.0f;
        if (nb->blink_hold <= 0 && def->eye_open > 0.4f && def->mouth != 2) {
            nb->blink_t = 0.13f;
        }
        else {
            apply_eye_open(nb, def->eye_open);
        }
    }

    /* ---- 眼神（待机左右看，正弦；其他按 def.look） ---- */
    float lx = def->look_x, ly = def->look_y;
    if (nb->emotion == NIMBO_EMOTION_IDLE) {
        lx = 6.0f * sinf(t * (2.0f * 3.14159f / 4.8f));
        ly = 2.0f * sinf(t * (2.0f * 3.14159f / 5.2f) + 1.1f);
    }
    lv_obj_set_pos(nb->eye_grp, (lv_coord_t)lx, (lv_coord_t)ly);

    /* ---- 眼睛大小（惊讶/聆听放大） ---- */
    int32_t sx = (int32_t)(def->eye_scale * 100);
    lv_obj_set_style_transform_scale_x(nb->eye_l, sx, 0);
    lv_obj_set_style_transform_scale_x(nb->eye_r, sx, 0);

    /* ---- 呼吸：整朵云上下浮动 ---- */
    float breath_period = (nb->emotion == NIMBO_EMOTION_SLEEPING) ? 5.5f : 3.8f;
    float breath_amp = (nb->emotion == NIMBO_EMOTION_HAPPY) ? 2.4f : 1.8f;
    if (nb->emotion == NIMBO_EMOTION_SLEEPING) breath_amp = 1.2f;
    float by = breath_amp * sinf(t * (2.0f * 3.14159f / breath_period));

    /* 开心/惊讶弹跳 */
    if (def->bounce) {
        float bp = 1.6f;
        float b = fabsf(sinf(t * (2.0f * 3.14159f / bp)));
        by -= b * 5.0f;
    }
    lv_obj_set_pos(nb->root, 0, (lv_coord_t)by);

    /* ---- 说话嘴动 ---- */
    if (def->mouth == 4) {
        float mk = 0.5f + 0.5f * sinf(t * (2.0f * 3.14159f / 0.28f));
        apply_mouth(nb, 4, mk);
    }
    else {
        apply_mouth(nb, def->mouth, 0.5f);
    }

    /* ---- 腮红 ---- */
    lv_obj_set_style_bg_opa(nb->blush_l, def->blush, 0);
    lv_obj_set_style_bg_opa(nb->blush_r, def->blush, 0);

    /* ---- 身体色调 ---- */
    lv_color_t bc = body_color_of(def->body_tint);
    for (int i = 0; i < 8; i++) {
        lv_obj_set_style_bg_color(nb->body[i], bc, 0);
    }

    /* ---- 睡觉 zzz ---- */
    if (def->zzz) {
        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(nb->zzz[i], LV_OBJ_FLAG_HIDDEN);
            float ph = fmodf(t * 0.6f + i * 0.33f, 1.0f);
            lv_obj_set_pos(nb->zzz[i], 176 + i * 10, 96 - (lv_coord_t)(ph * 52));
            lv_obj_set_style_opa(nb->zzz[i], (lv_opa_t)(255 * (1.0f - ph)), 0);
        }
    }
    else {
        for (int i = 0; i < 3; i++) {
            lv_obj_add_flag(nb->zzz[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* ==================== 公共接口 ==================== */

nimbo_t * nimbo_create(lv_obj_t * parent)
{
    nimbo_t * nb = (nimbo_t *)lv_malloc(sizeof(nimbo_t));
    if (!nb) return NULL;
    memset(nb, 0, sizeof(*nb));
    nb->emotion = NIMBO_EMOTION_IDLE;
    nb->blink_t = -1;
    nb->blink_hold = 2.5f;

    nb->root = lv_obj_create(parent);
    lv_obj_remove_flag(nb->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(nb->root, 240, 240);
    lv_obj_set_pos(nb->root, 0, 0);
    lv_obj_set_style_bg_opa(nb->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nb->root, 0, 0);
    lv_obj_set_style_pad_all(nb->root, 0, 0);

    /* 身体：双层圆 —— 先画外轮廓层（略大 2px），再叠实心身体层盖住中心，
     * 交叠处线条被上层盖掉，只保留最外侧轮廓 */
    for (int i = 0; i < 8; i++) {
        nb->body_out[i] = make_circle(nb->root, s_body_cx[i], s_body_cy[i],
                                      s_body_r[i] + 2, CLR_BODY_EDGE, CLR_BODY_EDGE, 0);
    }
    for (int i = 0; i < 8; i++) {
        nb->body[i] = make_circle(nb->root, s_body_cx[i], s_body_cy[i],
                                  s_body_r[i], CLR_BODY, CLR_BODY, 0);
    }

    /* 眼睛组（眼神移动） */
    nb->eye_grp = lv_obj_create(nb->root);
    lv_obj_remove_flag(nb->eye_grp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(nb->eye_grp, 240, 240);
    lv_obj_set_pos(nb->eye_grp, 0, 0);
    lv_obj_set_style_bg_opa(nb->eye_grp, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nb->eye_grp, 0, 0);
    lv_obj_set_style_pad_all(nb->eye_grp, 0, 0);

    int ex = 120 - EYE_GAP / 2 - EYE_W / 2;   /* 左眼 x=75 */
    int er = 120 + EYE_GAP / 2 - EYE_W / 2;   /* 右眼 x=105 */
    int ey = EYE_CY - EYE_H / 2;              /* 眼睛 y=105 */

    nb->eye_l = make_circle(nb->eye_grp, ex + EYE_W / 2, ey + EYE_H / 2, EYE_W / 2, CLR_EYE, CLR_EYE, 0);
    lv_obj_set_size(nb->eye_l, EYE_W, EYE_H);
    nb->eye_r = make_circle(nb->eye_grp, er + EYE_W / 2, ey + EYE_H / 2, EYE_W / 2, CLR_EYE, CLR_EYE, 0);
    lv_obj_set_size(nb->eye_r, EYE_W, EYE_H);
    /* 眼睛缩放中心 */
    lv_obj_set_style_transform_pivot_x(nb->eye_l, EYE_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(nb->eye_l, EYE_H / 2, 0);
    lv_obj_set_style_transform_pivot_x(nb->eye_r, EYE_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(nb->eye_r, EYE_H / 2, 0);

    /* 高光 */
    nb->hl_l = make_circle(nb->eye_l, 9, 9, 5, lv_color_white(), lv_color_white(), 0);
    nb->hl_r = make_circle(nb->eye_r, 9, 9, 5, lv_color_white(), lv_color_white(), 0);

    /* 腮红 */
    nb->blush_l = make_circle(nb->root, 120 - BLUSH_DX, EYE_CY + BLUSH_DY, BLUSH_R, CLR_BLUSH, CLR_BLUSH, 0);
    nb->blush_r = make_circle(nb->root, 120 + BLUSH_DX, EYE_CY + BLUSH_DY, BLUSH_R, CLR_BLUSH, CLR_BLUSH, 0);
    lv_obj_set_style_bg_opa(nb->blush_l, 0, 0);
    lv_obj_set_style_bg_opa(nb->blush_r, 0, 0);

    /* 嘴巴：arc（微笑/倒弧）+ obj（O/线/说话） */
    nb->mouth_arc = lv_arc_create(nb->root);
    lv_obj_set_size(nb->mouth_arc, MOUTH_W, MOUTH_W);
    lv_obj_set_pos(nb->mouth_arc, 120 - MOUTH_W / 2, MOUTH_CY - MOUTH_W / 2);
    lv_obj_remove_flag(nb->mouth_arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_value(nb->mouth_arc, 100);
    lv_arc_set_rotation(nb->mouth_arc, 0);
    lv_obj_set_style_bg_opa(nb->mouth_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(nb->mouth_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(nb->mouth_arc, CLR_MOUTH, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(nb->mouth_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(nb->mouth_arc, 1, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(nb->mouth_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_opa(nb->mouth_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    nb->mouth_obj = lv_obj_create(nb->root);
    lv_obj_remove_flag(nb->mouth_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(nb->mouth_obj, 120 - 11, MOUTH_CY - 11);
    lv_obj_set_size(nb->mouth_obj, 22, 22);
    lv_obj_set_style_radius(nb->mouth_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(nb->mouth_obj, CLR_MOUTH, 0);
    lv_obj_set_style_border_color(nb->mouth_obj, CLR_MOUTH, 0);
    lv_obj_set_style_border_width(nb->mouth_obj, 3, 0);
    lv_obj_set_style_pad_all(nb->mouth_obj, 0, 0);
    lv_obj_set_style_transform_pivot_x(nb->mouth_obj, 11, 0);
    lv_obj_set_style_transform_pivot_y(nb->mouth_obj, 11, 0);
    lv_obj_add_flag(nb->mouth_obj, LV_OBJ_FLAG_HIDDEN);

    /* 睡觉 zzz */
    for (int i = 0; i < 3; i++) {
        nb->zzz[i] = lv_label_create(nb->root);
        lv_label_set_text(nb->zzz[i], "z");
        lv_obj_set_style_text_color(nb->zzz[i], lv_color_hex(0x9FB3D6), 0);
        lv_obj_set_style_text_font(nb->zzz[i], &lv_font_montserrat_28, 0);
        lv_obj_set_pos(nb->zzz[i], 176 + i * 10, 96);
        lv_obj_add_flag(nb->zzz[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 初始表情 */
    nimbo_set_emotion(nb, NIMBO_EMOTION_IDLE);

    /* 动画定时器 */
    nb->timer = lv_timer_create(nimbo_timer_cb, NIMBO_TICK_MS, nb);

    return nb;
}

void nimbo_set_emotion(nimbo_t * nb, int emotion)
{
    if (!nb) return;
    if (emotion < 0) emotion = 0;
    if (emotion >= NIMBO_EMOTION_COUNT) emotion = NIMBO_EMOTION_COUNT - 1;
    nb->emotion = emotion;
    nb->blink_t = -1;
    nb->blink_hold = 2.0f + (float)(rand() % 2500) / 1000.0f;

    /* 立即应用静态参数（动画循环随后叠加） */
    const nimbo_emotion_def_t * def = &s_emotions[emotion];
    apply_eye_open(nb, def->eye_open);
    lv_obj_set_style_transform_scale_x(nb->eye_l, (int32_t)(def->eye_scale * 100), 0);
    lv_obj_set_style_transform_scale_x(nb->eye_r, (int32_t)(def->eye_scale * 100), 0);
    lv_obj_set_pos(nb->eye_grp, (lv_coord_t)def->look_x, (lv_coord_t)def->look_y);
    apply_mouth(nb, def->mouth, 0.5f);
    lv_obj_set_style_bg_opa(nb->blush_l, def->blush, 0);
    lv_obj_set_style_bg_opa(nb->blush_r, def->blush, 0);
    lv_color_t bc = body_color_of(def->body_tint);
    for (int i = 0; i < 8; i++) {
        lv_obj_set_style_bg_color(nb->body[i], bc, 0);
    }
    for (int i = 0; i < 3; i++) {
        if (def->zzz) lv_obj_clear_flag(nb->zzz[i], LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_add_flag(nb->zzz[i], LV_OBJ_FLAG_HIDDEN);
    }
}

int nimbo_get_emotion(const nimbo_t * nb)
{
    return nb ? nb->emotion : -1;
}

void nimbo_delete(nimbo_t * nb)
{
    if (!nb) return;
    if (nb->timer) lv_timer_delete(nb->timer);
    lv_obj_delete(nb->root);
    lv_free(nb);
}
