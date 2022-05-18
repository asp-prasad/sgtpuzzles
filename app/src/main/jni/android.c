/*
 * android.c: Android front end for my puzzle collection.
 */

#include <jni.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <math.h>

#include <sys/time.h>

#include "puzzles.h"
#include "android.h"

void fatal(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "fatal error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

#if 0
// TODO Better translation mechanism or give up on it.
/* This is so that the numerous callers of _() don't have to free strings.
 * Unfortunately they may need a few to be valid at a time (game config
 * dialogs). */
#define GETTEXTED_SIZE 256
#define GETTEXTED_COUNT 32
static char gettexted[GETTEXTED_COUNT][GETTEXTED_SIZE];
static int next_gettexted = 0;
#endif

static jobject ARROW_MODE_NONE = NULL,
	ARROW_MODE_ARROWS_ONLY = NULL,
	ARROW_MODE_ARROWS_LEFT_CLICK = NULL,
	ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK = NULL,
	ARROW_MODE_DIAGONALS = NULL;

static jclass GameEngineImpl = NULL, BackendName = NULL, MenuEntry = NULL, IllegalArgumentException = NULL, RectF = NULL, Point = NULL, CustomDialogBuilder = NULL, KeysResult = NULL;
static jfieldID frontendField;
static jmethodID
	newGameEngineImpl,
	newDialogBuilder,
	newKeysResult,
	blitterAlloc,
	blitterFree,
	blitterLoad,
	blitterSave,
	changedState,
	purgingStates,
	allowFlash,
	clipRect,
	dialogAddString,
	dialogAddBoolean,
	dialogAddChoices,
	dialogShow,
	drawCircle,
	drawLine,
	drawPoly,
	drawText,
	fillRect,
	getBackgroundColour,
	getText,
	postInvalidate,
	requestTimer,
	baosWrite,
	setStatus,
	unClip,
	completed,
	inertiaFollow,
	byDisplayName,
	backendToString,
	newRectFWithLTRB,
	newPoint;

void throwIllegalArgumentException(JNIEnv *env, const char* reason) {
	(*env)->ThrowNew(env, IllegalArgumentException, reason);
}

void get_random_seed(void **randseed, int *randseedsize)
{
	struct timeval *tvp = snew(struct timeval);
	gettimeofday(tvp, NULL);
	*randseed = (void *)tvp;
	*randseedsize = sizeof(struct timeval);
}

void frontend_default_colour(frontend *fe, float *output)
{
	if ((*fe->env)->ExceptionCheck(fe->env)) {
		output[0] = 1.0f; output[0] = 0.0f; output[0] = 0.0f;
		return;
	}
	jint argb = (*fe->env)->CallIntMethod(fe->env, fe->viewCallbacks, getBackgroundColour);
	output[0] = ((float)((argb & 0x00ff0000) >> 16)) / 255.0f;
	output[1] = ((float)((argb & 0x0000ff00) >> 8)) / 255.0f;
	output[2] = ((float)(argb & 0x000000ff)) / 255.0f;
}

void android_status_bar(void *handle, const char *text)
{
	frontend *fe = (frontend *)handle;
	jstring js = (*fe->env)->NewStringUTF(fe->env, text);
	if( js == NULL ) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, setStatus, js);
	(*fe->env)->DeleteLocalRef(fe->env, js);
}

void android_start_draw(__attribute__((unused)) void *handle)
{
}

void android_clip(void *handle, int x, int y, int w, int h)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, clipRect, x + fe->ox, y + fe->oy, w, h);
}

void android_unclip(void *handle)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, unClip, fe->ox, fe->oy);
}

void android_draw_text(void *handle, int x, int y, int fonttype, int fontsize,
		int align, int colour, const char *text)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	jstring js = (*fe->env)->NewStringUTF(fe->env, text);
	if( js == NULL ) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, drawText, x + fe->ox, y + fe->oy,
			(fonttype == FONT_FIXED ? 0x10 : 0x0) | align,
			fontsize, colour, js);
	(*fe->env)->DeleteLocalRef(fe->env, js);
}

void android_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, fillRect, x + fe->ox, y + fe->oy, w, h, colour);
}

void android_draw_thick_line(void *handle, float thickness, float x1, float y1, float x2, float y2, int colour)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, drawLine, thickness, x1 + (float)fe->ox, y1 + (float)fe->oy, x2 + (float)fe->ox, y2 + (float)fe->oy, colour);
}

void android_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
	android_draw_thick_line(handle, 1.f, (float)x1, (float)y1, (float)x2, (float)y2, colour);
}

void android_draw_thick_poly(void *handle, float thickness, const int *coords, int npoints,
		int fillColour, int outlineColour)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	jintArray coordsJava = (*fe->env)->NewIntArray(fe->env, npoints*2);
	if (coordsJava == NULL) return;
	(*fe->env)->SetIntArrayRegion(fe->env, coordsJava, 0, npoints*2, coords);
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, drawPoly, thickness, coordsJava, fe->ox, fe->oy, outlineColour, fillColour);
	(*fe->env)->DeleteLocalRef(fe->env, coordsJava);  // prevent ref table exhaustion on e.g. large Mines grids...
}

void android_draw_poly(void *handle, const int *coords, int npoints,
		int fillColour, int outlineColour)
{
	android_draw_thick_poly(handle, 1.f, coords, npoints, fillColour, outlineColour);
}

void android_draw_thick_circle(void *handle, float thickness, float cx, float cy, float radius, int fillColour, int outlineColour)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, drawCircle, thickness, cx + (float)fe->ox, cy + (float)fe->oy, radius, outlineColour, fillColour);
}

void android_draw_circle(void *handle, int cx, int cy, int radius, int fillColour, int outlineColour)
{
	android_draw_thick_circle(handle, 1.f, (float)cx, (float)cy, (float)radius, fillColour, outlineColour);
}

struct blitter {
	int handle, w, h, x, y;
};

blitter *android_blitter_new(__attribute__((unused)) void *handle, int w, int h)
{
	blitter *bl = snew(blitter);
	bl->handle = -1;
	bl->w = w;
	bl->h = h;
	return bl;
}

void android_blitter_free(void *handle, blitter *bl)
{
	if (bl->handle != -1) {
		frontend *fe = (frontend *)handle;
		if (!(*fe->env)->ExceptionCheck(fe->env)) {
			(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, blitterFree, bl->handle);
		}
	}
	sfree(bl);
}

void android_blitter_save(void *handle, blitter *bl, int x, int y)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	if (bl->handle == -1)
		bl->handle = (*fe->env)->CallIntMethod(fe->env, fe->viewCallbacks, blitterAlloc, bl->w, bl->h);
	bl->x = x;
	bl->y = y;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, blitterSave, bl->handle, x + fe->ox, y + fe->oy);
}

void android_blitter_load(void *handle, blitter *bl, int x, int y)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	assert(bl->handle != -1);
	if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
		x = bl->x;
		y = bl->y;
	}
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, blitterLoad, bl->handle, x + fe->ox, y + fe->oy);
}

void android_end_draw(void *handle)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->viewCallbacks, postInvalidate);
}

void android_changed_state(void *handle, int can_undo, int can_redo)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, changedState, can_undo, can_redo);
}

void android_purging_states(void *handle)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, purgingStates);
}

void android_inertia_follow(void *handle, bool is_solved)
{
	frontend *fe = (frontend *)handle;
	if ((*fe->env)->ExceptionCheck(fe->env)) return;
	(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, inertiaFollow, is_solved);
}

int allow_flash(frontend *fe)
{
	if ((*fe->env)->ExceptionCheck(fe->env)) return false;
	return (*fe->env)->CallBooleanMethod(fe->env, fe->activityCallbacks, allowFlash);
}

static char *android_text_fallback(__attribute__((unused)) void *handle, const char *const *strings,
				   __attribute__((unused)) int nStrings)
{
    /*
     * We assume Android can cope with any UTF-8 likely to be emitted
     * by a puzzle.
     */
    return dupstr(strings[0]);
}

const struct drawing_api android_drawing = {
	android_draw_text,
	android_draw_rect,
	android_draw_line,
	android_draw_poly,
	android_draw_thick_poly,
	android_draw_circle,
	android_draw_thick_circle,
	NULL, // draw_update,
	android_clip,
	android_unclip,
	android_start_draw,
	android_end_draw,
	android_status_bar,
	android_blitter_new,
	android_blitter_free,
	android_blitter_save,
	android_blitter_load,
	NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
	NULL, NULL,				   /* line_width, line_dotted */
	android_text_fallback,
	android_changed_state,
	android_purging_states,
	android_draw_thick_line,
        android_inertia_follow,
};

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_keyEvent(JNIEnv *env, jobject gameEngine, jint x, jint y, jint keyVal)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	if (fe->ox == -1 || keyVal < 0) return;
	midend_process_key(fe->me, x - fe->ox, y - fe->oy, keyVal);
}

JNIEXPORT jfloat JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_suggestDensity(JNIEnv *env, jobject gameEngine, jint viewWidth, jint viewHeight)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (!fe || !fe->me) return 1.f;
	fe->env = env;
	int defaultW = INT_MAX, defaultH = INT_MAX;
	midend_reset_tilesize(fe->me);
	midend_size(fe->me, &defaultW, &defaultH, false);
	return max(1.f, min(floor(((double)viewWidth) / defaultW), floor(((double)viewHeight) / defaultH)));
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_resizeEvent(JNIEnv *env, jobject gameEngine, jint viewWidth, jint viewHeight)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (!fe || !fe->me) return;
	fe->env = env;
	int w = viewWidth, h = viewHeight;
	midend_size(fe->me, &w, &h, true);
	fe->winwidth = w;
	fe->winheight = h;
	fe->ox = (viewWidth - w) / 2;
	fe->oy = (viewHeight - h) / 2;
	if (fe->viewCallbacks) (*env)->CallVoidMethod(env, fe->viewCallbacks, unClip, fe->ox, fe->oy);
	midend_force_redraw(fe->me);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_timerTick(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (! fe->timer_active) return;
	fe->env = env;
	struct timeval now;
	float elapsed;
	gettimeofday(&now, NULL);
	elapsed = (float)((now.tv_usec - fe->last_time.tv_usec) * 0.000001 +
			(now.tv_sec - fe->last_time.tv_sec));
		midend_timer(fe->me, elapsed);  // may clear timer_active
	fe->last_time = now;
}

void deactivate_timer(frontend *fe)
{
	if (!fe) return;
	if (fe->timer_active) {
		if ((*fe->env)->ExceptionCheck(fe->env)) return;
		(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, requestTimer, false);
	}
	fe->timer_active = false;
}

void activate_timer(frontend *fe)
{
	if (!fe) return;
	if (!fe->timer_active) {
		if ((*fe->env)->ExceptionCheck(fe->env)) return;
		(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, requestTimer, true);
		gettimeofday(&fe->last_time, NULL);
	}
	fe->timer_active = true;
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_resetTimerBaseline(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (!fe) return;
	fe->env = env;
	gettimeofday(&fe->last_time, NULL);
}

config_item* configItemWithName(frontend* fe, JNIEnv *env, jstring js)
{
	const char* name = (*env)->GetStringUTFChars(env, js, NULL);
	config_item* i;
	config_item* ret = NULL;
	for (i = fe->cfg; i->type != C_END; i++) {
		if (!strcmp(name, i->name)) {
			ret = i;
			break;
		}
	}
	(*env)->ReleaseStringUTFChars(env, js, name);
	return ret;
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_configSetString(JNIEnv *env, jobject gameEngine, jstring name, jstring s)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	config_item *i = configItemWithName(fe, env, name);
	const char* newval = (*env)->GetStringUTFChars(env, s, NULL);
	sfree(i->u.string.sval);
	i->u.string.sval = dupstr(newval);
	(*env)->ReleaseStringUTFChars(env, s, newval);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_configSetBool(JNIEnv *env, jobject gameEngine, jstring name, jint selected)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	config_item *i = configItemWithName(fe, env, name);
	i->u.boolean.bval = selected != 0 ? true : false;
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_configSetChoice(JNIEnv *env, jobject gameEngine, jstring name, jint selected)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	config_item *i = configItemWithName(fe, env, name);
	i->u.choices.selected = selected;
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_solveEvent(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	const char *msg = midend_solve(fe->me);
	if (! msg) return;
	jstring js = (*env)->NewStringUTF(env, msg);
	if( js == NULL ) return;
	throwIllegalArgumentException(env, msg);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_restartEvent(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	midend_restart_game(fe->me);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_configEvent(JNIEnv *env, jobject gameEngine, jobject activityCallbacks, jint whichEvent, jobject context, jobject backendEnum)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	char *title;
	config_item *i;
	fe->cfg = midend_get_config(fe->me, whichEvent, &title);
	fe->cfg_which = whichEvent;
	jstring js = (*env)->NewStringUTF(env, title);
	if( js == NULL ) return;
	jobject builder = (*env)->NewObject(env, CustomDialogBuilder, newDialogBuilder, context, gameEngine, activityCallbacks, whichEvent, js, backendEnum);
	if ((*env)->ExceptionCheck(env)) return;
	for (i = fe->cfg; i->type != C_END; i++) {
		jstring name = NULL;
		if (i->name) {
			name = (*env)->NewStringUTF(env, i->name);
			if (!name) return;
		}
		jstring sval = NULL;
		switch (i->type) {
			case C_STRING:
				if (i->u.string.sval) {
					sval = (*env)->NewStringUTF(env, i->u.string.sval);
					if (!sval) return;
				}
				if ((*env)->ExceptionCheck(env)) return;
				(*env)->CallVoidMethod(env, builder, dialogAddString, whichEvent, name, sval);
				break;
			case C_CHOICES:
				if (i->u.choices.choicenames) {
					sval = (*env)->NewStringUTF(env, i->u.choices.choicenames);
					if (!sval) return;
				}
				if ((*env)->ExceptionCheck(env)) return;
				(*env)->CallVoidMethod(env, builder, dialogAddChoices, whichEvent, name, sval, i->u.choices.selected);
				break;
			case C_BOOLEAN: case C_END: default:
				if ((*env)->ExceptionCheck(env)) return;
				(*env)->CallVoidMethod(env, builder, dialogAddBoolean, whichEvent, name, i->u.boolean.bval);
				break;
		}
		if (name) (*env)->DeleteLocalRef(env, name);
		if (sval) (*env)->DeleteLocalRef(env, sval);
	}
	if ((*env)->ExceptionCheck(env)) return;
	(*env)->CallVoidMethod(env, builder, dialogShow);
}

JNIEXPORT jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_configOK(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	char *encoded;
	const char *err = midend_config_to_encoded_params(fe->me, fe->cfg, &encoded);

	if (err) {
		throwIllegalArgumentException(env, err);
		return NULL;
	}

	free_cfg(fe->cfg);
	fe->cfg = NULL;

	jstring ret = (*env)->NewStringUTF(env, encoded);
	sfree(encoded);
	return ret;
}

jstring getDescOrSeedFromDialog(JNIEnv *env, jobject gameEngine, int mode)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	/* we must build a fully-specified string (with params) so GameLaunch knows params,
	   and in the case of seed, so the game gen process generates with correct params */
	char sep = (mode == CFG_SEED) ? (char)'#' : (char)':';
	char *buf;
	int free_buf = false;
	jstring ret = NULL;
	if (!strchr(fe->cfg[0].u.string.sval, sep)) {
		char *params = midend_get_current_params(fe->me, mode == CFG_SEED);
		size_t paramsLen = strlen(params);
		buf = snewn(paramsLen + strlen(fe->cfg[0].u.string.sval) + 2, char);
		sprintf(buf, "%s%c%s", params, sep, fe->cfg[0].u.string.sval);
		sfree(params);
		free_buf = true;
	} else {
		buf = fe->cfg[0].u.string.sval;
	}
	char *willBeMangled = dupstr(buf);
	const char *error = midend_game_id_int(fe->me, willBeMangled, mode, true);
	sfree(willBeMangled);
	if (!error) ret = (*env)->NewStringUTF(env, buf);
	if (free_buf) sfree(buf);
	if (error) {
		throwIllegalArgumentException(env, error);
	} else {
		free_cfg(fe->cfg);
		fe->cfg = NULL;
	}
	return ret;
}

JNIEXPORT jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getFullGameIDFromDialog(JNIEnv *env, jobject gameEngine)
{
	return getDescOrSeedFromDialog(env, gameEngine, CFG_DESC);
}

JNIEXPORT jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getFullSeedFromDialog(JNIEnv *env, jobject gameEngine)
{
	return getDescOrSeedFromDialog(env, gameEngine, CFG_SEED);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_configCancel(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	free_cfg(fe->cfg);
	fe->cfg = NULL;
}

struct serialise_ctx {
    JNIEnv *env;
    jobject baos;
};

void android_serialise_write(void *ctx, const void *buf, int len)
{
	struct serialise_ctx *sctx = (struct serialise_ctx *) ctx;
	JNIEnv *env = sctx->env;
	if ((*env)->ExceptionCheck(env)) return;
	jbyteArray bytesJava = (*env)->NewByteArray(env, len);
	if (bytesJava == NULL) return;
	(*env)->SetByteArrayRegion(env, bytesJava, 0, len, buf);
	(*env)->CallVoidMethod(env, sctx->baos, baosWrite, bytesJava);
	(*env)->DeleteLocalRef(env, bytesJava);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_serialise(JNIEnv *env, jobject gameEngine, jobject baos)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (!fe) return;
	fe->env = env;
	struct serialise_ctx sctx;
	sctx.env = env;
	sctx.baos = baos;
	midend_serialise(fe->me, android_serialise_write, &sctx);
}

struct deserialise_ctx {
    const char* deserialise_read_ptr;
    size_t deserialise_read_len;
};

bool android_deserialise_read(void *ctx, void *buf, int len)
{
	struct deserialise_ctx *dctx = (struct deserialise_ctx *)ctx;
	if (len < 0) return false;
	size_t l = min((size_t)len, dctx->deserialise_read_len);
	if (l == 0) return len == 0;
	memcpy(buf, dctx->deserialise_read_ptr, l );
	dctx->deserialise_read_ptr += l;
	dctx->deserialise_read_len -= l;
	return l == len;
}

jobject deserialiseOrIdentify(JNIEnv *env, frontend *new_fe, jstring s, jboolean identifyOnly) {
	const char * c = (*env)->GetStringUTFChars(env, s, NULL);
	struct deserialise_ctx dctx;
	dctx.deserialise_read_ptr = c;
	dctx.deserialise_read_len = strlen(dctx.deserialise_read_ptr);
	char *name;
	const char *error = identify_game(&name, android_deserialise_read, &dctx);
	const struct game* whichBackend = NULL;
	jobject backendEnum = NULL;
	if (! error) {
		int i;
		for (i = 0; i < gamecount; i++) {
			if (!strcmp(gamelist[i]->name, name)) {
				whichBackend = gamelist[i];
				backendEnum = (*env)->CallStaticObjectMethod(env, BackendName, byDisplayName, (*env)->NewStringUTF(env, name));
			}
		}
		if (whichBackend == NULL || backendEnum == NULL) error = "Internal error identifying game";
	}
	if (! error && ! identifyOnly) {
		new_fe->thegame = whichBackend;
		new_fe->me = midend_new(new_fe, whichBackend, &android_drawing, new_fe);
		dctx.deserialise_read_ptr = c;
		dctx.deserialise_read_len = strlen(dctx.deserialise_read_ptr);
		error = midend_deserialise(new_fe->me, android_deserialise_read, &dctx);
	}
	(*env)->ReleaseStringUTFChars(env, s, c);
	if (error) {
		throwIllegalArgumentException(env, error);
		if (!identifyOnly && new_fe->me) {
			midend_free(new_fe->me);
			new_fe->me = NULL;
		}
	}
	return backendEnum;
}

JNIEXPORT jobject JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_identifyBackend(JNIEnv *env, __attribute__((unused)) jclass clazz, jstring savedGame)
{
	return deserialiseOrIdentify(env, NULL, savedGame, true);
}

JNIEXPORT jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getCurrentParams(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (! fe || ! fe->me) return NULL;
	fe->env = env;
	char *params = midend_get_current_params(fe->me, true);
	jstring ret = (*env)->NewStringUTF(env, params);
	sfree(params);
	return ret;
}

JNIEXPORT jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_htmlHelpTopic(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (! fe || ! fe->me) return NULL;
	fe->env = env;
	return (*env)->NewStringUTF(env, fe->thegame->htmlhelp_topic);
}

void android_completed(frontend *fe)
{
	(*fe->env)->CallVoidMethod(fe->env, fe->activityCallbacks, completed);
}

const game* game_by_name(const char* name) {
	int i;
	for (i = 0; i<gamecount; i++) {
		if (!strcmp(name, gamenames[i])) {
			return gamelist[i];
		}
	}
	return NULL;
}

game_params* params_from_str(const game* my_game, const char* params_str, const char** error) {
	game_params *params = my_game->default_params();
	if (params_str != NULL) {
		my_game->decode_params(params, params_str);
	}
	const char *our_error = my_game->validate_params(params, true);
	if (our_error) {
		my_game->free_params(params);
		if (error) {
			(*error) = our_error;
		}
		return NULL;
	}
	return params;
}

const game* gameFromEnum(JNIEnv *env, jobject backendEnum)
{
    const jstring backendName = (jstring)(*env)->CallObjectMethod(env, backendEnum, backendToString);
    const char *backend = (*env)->GetStringUTFChars(env, backendName, NULL);
    const game *ret = game_by_name(backend);
    (*env)->ReleaseStringUTFChars(env, backendName, backend);
    return ret;
}

JNIEXPORT jobject JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_requestKeys(JNIEnv *env, jobject gameEngine, jobject backendEnum, jstring jParams)
{
	if ((*env)->ExceptionCheck(env)) return NULL;
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	const game *my_game = gameFromEnum(env, backendEnum);
	if (!my_game) {
		throwIllegalArgumentException(env, "Internal error identifying game in requestKeys");
		return NULL;
	}
	int nkeys = 0;
	const char *paramsStr = jParams ? (*env)->GetStringUTFChars(env, jParams, NULL) : NULL;
	game_params *params = params_from_str(my_game, paramsStr, NULL);
	if (jParams) (*env)->ReleaseStringUTFChars(env, jParams, paramsStr);
	if (!params) {
		return NULL;
	}
	int arrowMode;
	const key_label *keys = midend_request_keys_by_game(&nkeys, my_game, params, &arrowMode);
	char *keyChars = snewn(nkeys + 1, char);
	char *keyCharsIfArrows = snewn(nkeys + 1, char);
	int pos = 0, posIfArrows = 0;
	for (int i = 0; i < nkeys; i++) {
		if (keys[i].needs_arrows) {
			keyCharsIfArrows[posIfArrows++] = (char)keys[i].button;
		} else {
			keyChars[pos++] = (char)keys[i].button;
		}
	}
	keyChars[pos] = '\0';
	keyCharsIfArrows[posIfArrows] = '\0';
	jstring jKeys = (*env)->NewStringUTF(env, keyChars);
	jstring jKeysIfArrows = (*env)->NewStringUTF(env, keyCharsIfArrows);
	jobject jArrowMode = (arrowMode == ANDROID_ARROWS_DIAGONALS) ? ARROW_MODE_DIAGONALS :
			(arrowMode == ANDROID_ARROWS_LEFT_RIGHT) ? ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK :
			(arrowMode == ANDROID_ARROWS_LEFT) ? ARROW_MODE_ARROWS_LEFT_CLICK :
			(arrowMode == ANDROID_ARROWS_ONLY) ? ARROW_MODE_ARROWS_ONLY :
			ARROW_MODE_NONE;
	jobject result = (*env)->NewObject(env, KeysResult, newKeysResult, jKeys, jKeysIfArrows, jArrowMode);
	sfree(keyChars);
	sfree(keyCharsIfArrows);
	sfree(params);
	return result;
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_setCursorVisibility(JNIEnv *env, jobject gameEngine, jboolean visible)
{
	if ((*env)->ExceptionCheck(env)) return;
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	if (!fe->me) return;
	midend_android_cursor_visibility(fe->me, visible);
}

#if 0
char * get_text(const char *s)
{
	if (!s || ! s[0] || !fe) return (char*)s;  // slightly naughty cast...
	if ((*env)->ExceptionCheck(env)) return;
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	jstring j = (jstring)(*env)->CallObjectMethod(env, obj, getText, (*env)->NewStringUTF(env, s));
	const char * c = (*env)->GetStringUTFChars(env, j, NULL);
	// TODO get rid of this horrible hack
	char * ret = gettexted[next_gettexted];
	next_gettexted = (next_gettexted + 1) % GETTEXTED_COUNT;
	strncpy(ret, c, GETTEXTED_SIZE);
	ret[GETTEXTED_SIZE-1] = '\0';
	(*env)->ReleaseStringUTFChars(env, j, c);
	return ret;
}
#endif

void startPlayingIntGameID(JNIEnv *env, frontend* new_fe, jstring jsGameID, jobject backendEnum)
{
	new_fe->thegame = gameFromEnum(env, backendEnum);
	if (!new_fe->thegame) {
		throwIllegalArgumentException(env, "Internal error identifying game in startPlayingIntGameID");
		return;
	}
	new_fe->me = midend_new(new_fe, new_fe->thegame, &android_drawing, new_fe);
	const char * gameIDjs = (*env)->GetStringUTFChars(env, jsGameID, NULL);
	char * gameID = dupstr(gameIDjs);
	(*env)->ReleaseStringUTFChars(env, jsGameID, gameIDjs);
	const char * error = midend_game_id(new_fe->me, gameID);
	sfree(gameID);
	if (error) {
		throwIllegalArgumentException(env, error);
		return;
	}
	midend_new_game(new_fe->me);
}

JNIEXPORT jfloatArray JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getColours(JNIEnv *env, jobject gameEngine)
{
	if ((*env)->ExceptionCheck(env)) return NULL;
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	if (!fe->me) return NULL;
	int n;
	float* colours;
	colours = midend_colours(fe->me, &n);
	jfloatArray jColours = (*env)->NewFloatArray(env, n*3);
	if (jColours == NULL) return NULL;
	(*env)->SetFloatArrayRegion(env, jColours, 0, n*3, colours);
	return jColours;
}

jobject getPresetInternal(JNIEnv *env, frontend *fe, struct preset_menu_entry entry);

jobjectArray getPresetsInternal(JNIEnv *env, frontend *fe, struct preset_menu *menu) {
    jobjectArray ret = (*env)->NewObjectArray(env, menu->n_entries, MenuEntry, NULL);
    for (int i = 0; i < menu->n_entries; i++) {
        jobject menuItem = getPresetInternal(env, fe, menu->entries[i]);
        (*env)->SetObjectArrayElement(env, ret, i, menuItem);
    }
    return ret;
}

jobject getPresetInternal(JNIEnv *env, frontend *fe, const struct preset_menu_entry entry) {
    jstring title = (*env)->NewStringUTF(env, entry.title);
    if (entry.submenu) {
        jobject submenu = getPresetsInternal(env, fe, entry.submenu);
        jmethodID newEntryWithSubmenu = (*env)->GetMethodID(env, MenuEntry,  "<init>", "(ILjava/lang/String;[Lname/boyle/chris/sgtpuzzles/MenuEntry;)V");
        return (*env)->NewObject(env, MenuEntry, newEntryWithSubmenu, entry.id, title, submenu);
    } else {
        jstring params = (*env)->NewStringUTF(env, midend_android_preset_menu_get_encoded_params(fe->me, entry.id));
        jmethodID newEntryWithParams = (*env)->GetMethodID(env, MenuEntry,  "<init>", "(ILjava/lang/String;Ljava/lang/String;)V");
        return (*env)->NewObject(env, MenuEntry, newEntryWithParams, entry.id, title, params);
    }
}

JNIEXPORT jobjectArray JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getPresets(JNIEnv *env, jobject gameEngine)
{
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	struct preset_menu* menu = midend_get_presets(fe->me, NULL);
	return getPresetsInternal(env, fe, menu);
}

JNIEXPORT jint JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getUIVisibility(JNIEnv *env, jobject gameEngine) {
	if ((*env)->ExceptionCheck(env)) return 0;
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	return (midend_can_undo(fe->me))
			+ (midend_can_redo(fe->me) << 1)
			+ (fe->thegame->can_configure << 2)
			+ (fe->thegame->can_solve << 3)
			+ (midend_wants_statusbar(fe->me) << 4);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_onDestroy(JNIEnv *env, jobject gameEngine) {
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	if (!fe) return;
	fe->env = env;
	if (fe->me) midend_free(fe->me);  // might use viewCallbacks (e.g. blitters)
	sfree(fe);
}

jobject startPlayingInt(JNIEnv *env, jobject backend, jobject activityCallbacks, jobject viewCallbacks, jstring saveOrGameID, int isGameID)
{
	frontend *new_fe = snew(frontend);
	memset(new_fe, 0, sizeof(frontend));
	new_fe->env = env;
	new_fe->ox = -1;
	new_fe->activityCallbacks = (*env)->NewGlobalRef(env, activityCallbacks);
	new_fe->viewCallbacks = (*env)->NewGlobalRef(env, viewCallbacks);
	if (isGameID) {
		startPlayingIntGameID(env, new_fe, saveOrGameID, backend);
	} else {
		backend = deserialiseOrIdentify(env, new_fe, saveOrGameID, false);
		if ((*env)->ExceptionCheck(env)) return NULL;
	}

	int x, y;
	x = INT_MAX;
	y = INT_MAX;
	midend_size(new_fe->me, &x, &y, false);
	return (*env)->NewObject(env, GameEngineImpl, newGameEngineImpl, (jlong) new_fe, backend);
}

JNIEXPORT jobject JNICALL
Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_fromSavedGame(JNIEnv *env, __attribute__((unused)) jclass clazz, jstring savedGame, jobject activityCallbacks, jobject viewCallbacks) {
	return startPlayingInt(env, NULL, activityCallbacks, viewCallbacks, savedGame, false);
}

JNIEXPORT jobject JNICALL
Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_fromGameID(JNIEnv *env, __attribute__((unused)) jclass clazz, jstring gameID, jobject backend, jobject activityCallbacks, jobject viewCallbacks)
{
	return startPlayingInt(env, backend, activityCallbacks, viewCallbacks, gameID, true);
}

JNIEXPORT jstring JNICALL
Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getDefaultParams(JNIEnv *env,
                                                                 __attribute__((unused)) jclass clazz,
                                                                 jobject backendEnum) {
    const game * g = gameFromEnum(env, backendEnum);
    if (!g) {
        throwIllegalArgumentException(env, "Internal error identifying game in getDefaultParams");
        return NULL;
    }
    game_params *params = g->default_params();
    char * encoded = g->encode_params(params, true);
    return (*env)->NewStringUTF(env, encoded);
}

JNIEXPORT void JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_purgeStates(JNIEnv *env, jobject gameEngine)
{
    if ((*env)->ExceptionCheck(env)) return;
    frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
    fe->env = env;
    if (fe->me) {
        midend_purge_states(fe->me);
    }
}

JNIEXPORT jboolean JNICALL Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_isCompletedNow(JNIEnv *env, jobject gameEngine) {
    if ((*env)->ExceptionCheck(env)) return false;
    frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
    fe->env = env;
    return fe->me && midend_status(fe->me) ? true : false;
}

JNIEXPORT jobject JNICALL
Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getCursorLocation(JNIEnv *env, jobject gameEngine) {
    int x, y, w, h;
    if ((*env)->ExceptionCheck(env)) return false;
    frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
    fe->env = env;
    if (!fe->me) {
        return NULL;
    }
    if (!midend_get_cursor_location(fe->me, &x, &y, &w, &h)) {
        return NULL;
    }
    return (*env)->NewObject(env, RectF, newRectFWithLTRB,
            (float)(fe->ox + x), (float)(fe->oy + y), (float)(fe->ox + x + w), (float)(fe->oy + y + h));
}

JNIEXPORT jobject JNICALL
Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_getGameSizeInGameCoords(JNIEnv *env, jobject gameEngine) {
    if ((*env)->ExceptionCheck(env)) return false;
    frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
    fe->env = env;
    return (*env)->NewObject(env, Point, newPoint, fe->winwidth, fe->winheight);
}


JNIEXPORT void JNICALL
Java_name_boyle_chris_sgtpuzzles_GameEngineImpl_freezePartialRedo(JNIEnv *env, jobject gameEngine) {
	if ((*env)->ExceptionCheck(env)) return;
	frontend* fe = (frontend *)(*env)->GetLongField(env, gameEngine, frontendField);
	fe->env = env;
	if (fe->me) {
		midend_process_key(fe->me, 0, 0, 'r');
		midend_freeze_timer(fe->me, 0.3f);
	}
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, __attribute__((unused)) void *reserved)
{
	jclass ActivityCallbacks, ViewCallbacks, ArrowMode;
	JNIEnv *env;
	if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_6)) return JNI_ERR;

	GameEngineImpl = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GameEngineImpl"));
	ActivityCallbacks = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GameEngine$ActivityCallbacks"));
	ViewCallbacks = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GameEngine$ViewCallbacks"));
	ArrowMode = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode"));
	BackendName = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/BackendName"));
	MenuEntry = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/MenuEntry"));
	CustomDialogBuilder = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/CustomDialogBuilder"));
	KeysResult = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GameEngine$KeysResult"));
	IllegalArgumentException = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "java/lang/IllegalArgumentException"));
	RectF = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "android/graphics/RectF"));
	Point = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "android/graphics/Point"));

	frontendField   = (*env)->GetFieldID(env, GameEngineImpl, "_nativeFrontend", "J");
	ARROW_MODE_NONE = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, ArrowMode,
			(*env)->GetStaticFieldID(env, ArrowMode, "NO_ARROWS", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_ARROWS_ONLY = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, ArrowMode,
			(*env)->GetStaticFieldID(env, ArrowMode, "ARROWS_ONLY", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_ARROWS_LEFT_CLICK = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, ArrowMode,
			(*env)->GetStaticFieldID(env, ArrowMode, "ARROWS_LEFT_CLICK", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, ArrowMode,
			(*env)->GetStaticFieldID(env, ArrowMode, "ARROWS_LEFT_RIGHT_CLICK", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_DIAGONALS = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, ArrowMode,
			(*env)->GetStaticFieldID(env, ArrowMode, "ARROWS_DIAGONALS", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));

	newGameEngineImpl  = (*env)->GetMethodID(env, GameEngineImpl, "<init>", "(JLname/boyle/chris/sgtpuzzles/BackendName;)V");
	byDisplayName  = (*env)->GetStaticMethodID(env, BackendName, "byDisplayName", "(Ljava/lang/String;)Lname/boyle/chris/sgtpuzzles/BackendName;");
	backendToString = (*env)->GetMethodID(env, BackendName, "toString", "()Ljava/lang/String;");
	newDialogBuilder = (*env)->GetMethodID(env, CustomDialogBuilder, "<init>",
		"(Landroid/content/Context;Lname/boyle/chris/sgtpuzzles/CustomDialogBuilder$EngineCallbacks;Lname/boyle/chris/sgtpuzzles/CustomDialogBuilder$ActivityCallbacks;ILjava/lang/String;Lname/boyle/chris/sgtpuzzles/BackendName;)V");
	newKeysResult  = (*env)->GetMethodID(env, KeysResult, "<init>",
			"(Ljava/lang/String;Ljava/lang/String;Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;)V");
	changedState   = (*env)->GetMethodID(env, ActivityCallbacks, "changedState", "(ZZ)V");
	purgingStates  = (*env)->GetMethodID(env, ActivityCallbacks, "purgingStates", "()V");
	allowFlash     = (*env)->GetMethodID(env, ActivityCallbacks, "allowFlash", "()Z");
	requestTimer   = (*env)->GetMethodID(env, ActivityCallbacks, "requestTimer", "(Z)V");
	setStatus      = (*env)->GetMethodID(env, ActivityCallbacks, "setStatus", "(Ljava/lang/String;)V");
	completed      = (*env)->GetMethodID(env, ActivityCallbacks, "completed", "()V");
	inertiaFollow  = (*env)->GetMethodID(env, ActivityCallbacks, "inertiaFollow", "(Z)V");
	//getText        = (*env)->GetMethodID(env, ActivityCallbacks, "gettext", "(Ljava/lang/String;)Ljava/lang/String;");
	blitterAlloc   = (*env)->GetMethodID(env, ViewCallbacks, "blitterAlloc", "(II)I");
	blitterFree    = (*env)->GetMethodID(env, ViewCallbacks, "blitterFree", "(I)V");
	blitterLoad    = (*env)->GetMethodID(env, ViewCallbacks, "blitterLoad", "(III)V");
	blitterSave    = (*env)->GetMethodID(env, ViewCallbacks, "blitterSave", "(III)V");
	clipRect       = (*env)->GetMethodID(env, ViewCallbacks, "clipRect", "(IIII)V");
	drawCircle     = (*env)->GetMethodID(env, ViewCallbacks, "drawCircle", "(FFFFII)V");
	drawLine       = (*env)->GetMethodID(env, ViewCallbacks, "drawLine", "(FFFFFI)V");
	drawPoly       = (*env)->GetMethodID(env, ViewCallbacks, "drawPoly", "(F[IIIII)V");
	drawText       = (*env)->GetMethodID(env, ViewCallbacks, "drawText", "(IIIIILjava/lang/String;)V");
	fillRect       = (*env)->GetMethodID(env, ViewCallbacks, "fillRect", "(IIIII)V");
	getBackgroundColour = (*env)->GetMethodID(env, ViewCallbacks, "getDefaultBackgroundColour", "()I");
	postInvalidate = (*env)->GetMethodID(env, ViewCallbacks, "postInvalidateOnAnimation", "()V");
	unClip         = (*env)->GetMethodID(env, ViewCallbacks, "unClip", "(II)V");
	dialogAddString = (*env)->GetMethodID(env, CustomDialogBuilder, "dialogAddString", "(ILjava/lang/String;Ljava/lang/String;)V");
	dialogAddBoolean = (*env)->GetMethodID(env, CustomDialogBuilder, "dialogAddBoolean", "(ILjava/lang/String;Z)V");
	dialogAddChoices = (*env)->GetMethodID(env, CustomDialogBuilder, "dialogAddChoices", "(ILjava/lang/String;Ljava/lang/String;I)V");
	dialogShow     = (*env)->GetMethodID(env, CustomDialogBuilder, "dialogShow", "()V");
	baosWrite      = (*env)->GetMethodID(env, (*env)->FindClass(env, "java/io/ByteArrayOutputStream"),  "write", "([B)V");
	newRectFWithLTRB = (*env)->GetMethodID(env, RectF, "<init>", "(FFFF)V");
	newPoint        = (*env)->GetMethodID(env, Point, "<init>", "(II)V");

	return JNI_VERSION_1_6;
}
