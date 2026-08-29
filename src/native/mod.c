// neuronmod - runtime IL2CPP mod for Yu-Gi-Oh! Neuron (jp.konami.YugiohOcgSupports 4.12.0)
#include <jni.h>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define TAG "NEURONMOD"
#include <dirent.h>
#include <setjmp.h>
#include <signal.h>

#define INSTR_PORT 24243

static JavaVM *g_vm;
static jclass g_toaster;
static jmethodID g_show;

/* show an on-screen toast; safe to call from any thread */
static void toast(const char *msg) {
    if (!g_vm || !g_toaster || !g_show) return;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv(g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != 0) return;
        attached = 1;
    }
    jstring js = (*env)->NewStringUTF(env, msg);
    (*env)->CallStaticVoidMethod(env, g_toaster, g_show, js, JNI_TRUE);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, js);
    if (attached) (*g_vm)->DetachCurrentThread(g_vm);
}
static void nlog(const char *fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", buf);
}

/* ---------- module base ---------- */
struct find_ctx { const char *name; uintptr_t base; };
static int phdr_cb(struct dl_phdr_info *i, size_t s, void *d) {
    struct find_ctx *c = d;
    if (i->dlpi_name && strstr(i->dlpi_name, c->name)) { c->base = i->dlpi_addr; return 1; }
    return 0;
}
static uintptr_t module_base(const char *n) {
    struct find_ctx c = { n, 0 }; dl_iterate_phdr(phdr_cb, &c); return c.base;
}

/* ---------- il2cpp api ---------- */
static void *L;
static void *(*il2cpp_domain_get)(void);
static void *(*il2cpp_thread_attach)(void *);
static void *(*il2cpp_domain_assembly_open)(void *, const char *);
static void *(*il2cpp_assembly_get_image)(void *);
static void *(*il2cpp_class_from_name)(void *, const char *, const char *);
static void *(*il2cpp_class_get_method_from_name)(void *, const char *, int);
static void *(*il2cpp_class_get_fields)(void *, void **);
static const char *(*il2cpp_field_get_name)(void *);
static size_t (*il2cpp_field_get_offset)(void *);
static void *(*il2cpp_field_get_type)(void *);
static char *(*il2cpp_type_get_name)(void *);
static void *(*il2cpp_object_get_class)(void *);
static const char *(*il2cpp_class_get_name)(void *);
static void *(*il2cpp_runtime_invoke)(void *, void *, void **, void **);
static void *(*il2cpp_object_unbox)(void *);
static void *(*il2cpp_class_get_methods)(void *, void **);
static const char *(*il2cpp_method_get_name)(void *);
static uint32_t (*il2cpp_method_get_param_count)(void *);
static void *(*il2cpp_method_get_param)(void *, uint32_t);
static int (*il2cpp_method_is_generic)(void *);
static void *(*il2cpp_type_get_object)(void *);
static void *(*il2cpp_class_get_type)(void *);
static void *(*il2cpp_string_new)(const char *);
static void *(*il2cpp_object_new)(void *);
static void *(*il2cpp_array_new)(void *, uintptr_t);
static int (*il2cpp_array_length)(void *);
static size_t (*il2cpp_image_get_class_count)(void *);
static void *(*il2cpp_image_get_class)(void *, size_t);
static void *(*il2cpp_class_get_field_from_name)(void *, const char *);
static void *(*il2cpp_class_get_parent)(void *);
static uint16_t *(*il2cpp_string_chars)(void *);
static int (*il2cpp_string_length)(void *);

#define SYM(x) do { *(void **)&x = dlsym(L, #x); if (!x) nlog("MISSING " #x); } while (0)

static uintptr_t g_base;
static void *g_domain, *g_img_cs, *g_img_core;

static void *cls(void *img, const char *ns, const char *n) {
    void *k = il2cpp_class_from_name(img, ns, n);
    if (!k) nlog("class not found: %s.%s", ns, n);
    return k;
}
/* pick the overload whose first parameter type matches, skipping generics */
static void *meth_sig(void *k, const char *n, int argc, const char *p0type) {
    void *iter = NULL, *m;
    while ((m = il2cpp_class_get_methods(k, &iter))) {
        if (strcmp(il2cpp_method_get_name(m), n)) continue;
        if ((int)il2cpp_method_get_param_count(m) != argc) continue;
        if (il2cpp_method_is_generic(m)) continue;
        if (p0type && argc > 0) {
            char *tn = il2cpp_type_get_name(il2cpp_method_get_param(m, 0));
            int ok = tn && !strcmp(tn, p0type);
            if (!ok) continue;
        }
        return m;
    }
    nlog("meth_sig miss: %s(%d, %s)", n, argc, p0type ? p0type : "-");
    return NULL;
}

static void *meth(void *k, const char *n, int argc) {
    void *m = k ? il2cpp_class_get_method_from_name(k, n, argc) : NULL;
    if (!m) nlog("method not found: %s/%d", n, argc);
    return m;
}
static void *inv(void *m, void *obj, void **args) {
    void *exc = NULL;
    void *r = il2cpp_runtime_invoke(m, obj, args, &exc);
    if (exc) nlog("  ! exception invoking method");
    return r;
}
static void cs_str(void *s, char *out, int n) {
    out[0] = 0;
    if (!s) { snprintf(out, n, "(null)"); return; }
    uint16_t *c = il2cpp_string_chars(s);
    int len = il2cpp_string_length(s), i;
    if (len > n - 1) len = n - 1;
    for (i = 0; i < len; i++) out[i] = (c[i] < 128) ? (char)c[i] : '?';
    out[i] = 0;
}

/* cached Unity methods */
static void *m_get_transform, *m_get_childCount, *m_GetChild, *m_get_name, *m_get_gameObject;
static void *k_Transform, *k_Object, *k_Component, *k_GameObject;
static void *m_go_get_transform, *m_Instantiate, *m_SetParent, *m_set_name, *m_GetComponent;
static void *k_TMP, *m_set_text, *m_get_text, *m_GetComponents;
static void *m_tmp_autosize, *m_tmp_fsMin, *m_tmp_fsMax, *m_tmp_getFs, *m_tmp_setFs;
static void *m_tmp_prefW;
static void *k_BindingText, *k_Behaviour, *m_set_enabled, *m_Destroy;
static void *m_get_position, *m_set_position, *m_rt_set_sizeDelta, *m_rt_rect, *m_GO_ctor, *m_AddComponent, *m_set_localPos;
static void *m_rt_set_anchorMin, *m_rt_set_anchorMax, *m_rt_set_anchoredPos, *m_rt_set_pivot, *m_set_localScale;
static void *k_RectTransform, *m_rt_anchorMin, *m_rt_anchorMax, *m_rt_anchoredPos, *m_rt_sizeDelta, *m_rt_pivot, *m_localScale;
static void *m_SetActive, *k_Button, *k_Image, *m_get_sprite, *m_set_sprite;
static void *m_activeInHierarchy;
static void *k_CalcSettings, *f_CalcMode, *m_OnClickCalcMode2;
static void *k_Screen, *m_screen_w, *m_screen_h, *m_screen_sa;
static void *k_Vector3, *m_GetWorldCorners;
static float g_shiftUnits, g_halfUnits, g_edgeUnits;
static void *g_onSprite, *g_offSprite;
static void *g_container;
static void *g_wrappedArea;    /* LifeArea we have already arranged */          /* the mode row container */
static int   g_selMode = -1;       /* mode currently shown as selected */
static void (*orig_OnClickCalcMode)(void *, int, void *);
static void *(*il2cpp_field_static_get_value)(void *, void *);
static void *(*il2cpp_field_static_set_value)(void *, void *);

static void unity_init(void) {
    void *asm_core = il2cpp_domain_assembly_open(g_domain, "UnityEngine.CoreModule");
    g_img_core = asm_core ? il2cpp_assembly_get_image(asm_core) : NULL;
    nlog("UnityEngine.CoreModule image = %p", g_img_core);
    k_Component  = cls(g_img_core, "UnityEngine", "Component");
    k_Transform  = cls(g_img_core, "UnityEngine", "Transform");
    k_Object     = cls(g_img_core, "UnityEngine", "Object");
    k_GameObject = cls(g_img_core, "UnityEngine", "GameObject");
    m_get_transform  = meth(k_Component, "get_transform", 0);
    m_get_gameObject = meth(k_Component, "get_gameObject", 0);
    m_get_childCount = meth(k_Transform, "get_childCount", 0);
    m_GetChild       = meth(k_Transform, "GetChild", 1);
    m_get_name       = meth(k_Object, "get_name", 0);
    m_set_name       = meth(k_Object, "set_name", 1);
    m_Instantiate    = meth(k_Object, "Instantiate", 1);
    m_go_get_transform = meth(k_GameObject, "get_transform", 0);
    m_SetParent      = meth(k_Transform, "SetParent", 2);
    m_GetComponent   = meth_sig(k_Component, "GetComponent", 1, "System.Type");
    m_GetComponents  = meth_sig(k_GameObject, "GetComponents", 1, "System.Type");
    k_Screen   = cls(g_img_core, "UnityEngine", "Screen");
    m_screen_w = meth(k_Screen, "get_width", 0);
    m_screen_h = meth(k_Screen, "get_height", 0);
    m_screen_sa = meth(k_Screen, "get_safeArea", 0);
    k_RectTransform  = cls(g_img_core, "UnityEngine", "RectTransform");
    m_rt_anchorMin   = meth(k_RectTransform, "get_anchorMin", 0);
    m_rt_anchorMax   = meth(k_RectTransform, "get_anchorMax", 0);
    m_rt_anchoredPos = meth(k_RectTransform, "get_anchoredPosition", 0);
    m_rt_sizeDelta   = meth(k_RectTransform, "get_sizeDelta", 0);
    m_rt_pivot       = meth(k_RectTransform, "get_pivot", 0);
    m_localScale     = meth(k_Transform, "get_localScale", 0);
    m_rt_set_anchorMin   = meth(k_RectTransform, "set_anchorMin", 1);
    m_rt_set_anchorMax   = meth(k_RectTransform, "set_anchorMax", 1);
    m_rt_set_anchoredPos = meth(k_RectTransform, "set_anchoredPosition", 1);
    m_rt_set_pivot       = meth(k_RectTransform, "set_pivot", 1);
    m_rt_set_sizeDelta   = meth(k_RectTransform, "set_sizeDelta", 1);
    m_get_position       = meth(k_Transform, "get_position", 0);
    m_set_position       = meth(k_Transform, "set_position", 1);
    m_set_localScale     = meth(k_Transform, "set_localScale", 1);
    m_set_localPos       = meth(k_Transform, "set_localPosition", 1);
    m_rt_rect            = meth(k_RectTransform, "get_rect", 0);
    m_GetWorldCorners    = meth(k_RectTransform, "GetWorldCorners", 1);
    k_Vector3            = cls(g_img_core, "UnityEngine", "Vector3");
    m_GO_ctor            = meth(k_GameObject, ".ctor", 1);
    m_AddComponent       = meth_sig(k_GameObject, "AddComponent", 1, "System.Type");
    nlog("wrapper deps: GO.ctor=%p AddComponent=%p get_rect=%p", m_GO_ctor, m_AddComponent, m_rt_rect);
    nlog("layout setters: anchorMin=%p anchoredPos=%p localScale=%p",
         m_rt_set_anchorMin, m_rt_set_anchoredPos, m_set_localScale);
    void *atmp = il2cpp_domain_assembly_open(g_domain, "Unity.TextMeshPro");
    void *itmp = atmp ? il2cpp_assembly_get_image(atmp) : NULL;
    k_TMP = itmp ? cls(itmp, "TMPro", "TMP_Text") : NULL;
    m_set_text = k_TMP ? meth(k_TMP, "set_text", 1) : NULL;
    m_get_text = k_TMP ? meth(k_TMP, "get_text", 0) : NULL;
    m_tmp_autosize = k_TMP ? meth(k_TMP, "set_enableAutoSizing", 1) : NULL;
    m_tmp_fsMin    = k_TMP ? meth(k_TMP, "set_fontSizeMin", 1) : NULL;
    m_tmp_fsMax    = k_TMP ? meth(k_TMP, "set_fontSizeMax", 1) : NULL;
    m_tmp_getFs    = k_TMP ? meth(k_TMP, "get_fontSize", 0) : NULL;
    m_tmp_setFs    = k_TMP ? meth(k_TMP, "set_fontSize", 1) : NULL;
    m_tmp_prefW    = k_TMP ? meth(k_TMP, "get_preferredWidth", 0) : NULL;
    nlog("TMP image=%p TMP_Text=%p set_text=%p", itmp, k_TMP, m_set_text);
    k_Behaviour   = cls(g_img_core, "UnityEngine", "Behaviour");
    m_set_enabled = k_Behaviour ? meth(k_Behaviour, "set_enabled", 1) : NULL;
    m_Destroy     = meth_sig(k_Object, "Destroy", 1, "UnityEngine.Object");
    k_BindingText = cls(g_img_cs, "UISystem.LocalizeText", "BindingText");
    void *aui = il2cpp_domain_assembly_open(g_domain, "UnityEngine.UI");
    void *iui = aui ? il2cpp_assembly_get_image(aui) : NULL;
    k_Button = iui ? cls(iui, "UnityEngine.UI", "Button") : NULL;
    k_Image  = iui ? cls(iui, "UnityEngine.UI", "Image") : NULL;
    m_SetActive = meth(k_GameObject, "SetActive", 1);
    m_activeInHierarchy = meth(k_GameObject, "get_activeInHierarchy", 0);
    m_get_sprite = k_Image ? meth(k_Image, "get_sprite", 0) : NULL;
    m_set_sprite = k_Image ? meth(k_Image, "set_sprite", 1) : NULL;
    k_CalcSettings = cls(g_img_cs, "", "CalculatorSettings");
    f_CalcMode = k_CalcSettings ? il2cpp_class_get_field_from_name(k_CalcSettings, "g_CalcMode") : NULL;
    void *kcalc = cls(g_img_cs, "", "Calculator");
    m_OnClickCalcMode2 = kcalc ? meth(kcalc, "OnClickCalcMode", 2) : NULL;
    nlog("Button=%p Image=%p set_sprite=%p CalculatorSettings=%p g_CalcMode=%p",
         k_Button, k_Image, m_set_sprite, k_CalcSettings, f_CalcMode);
    nlog("BindingText=%p set_enabled=%p Destroy=%p", k_BindingText, m_set_enabled, m_Destroy);
}

static void *tf_child(void *tf, int i);
static void *tf_parent(void *tf);
static int   sibling_index(void *tf);
static void  set_sibling(void *tf, int idx);
static void  reparent(void *tf, void *parent);
static void  discard(void *tf);
static void  rect_size(void *tf, float *w, float *h);
static void  set_active(void *tf, int on);
static void  copy_rect_props(void *src, void *dst);
static int   world_pos(void *tf, float *x, float *y, float *z);
static void  set_world(void *tf, float x, float y, float z);

/* address of a field by name, walking up the class hierarchy */
static void *fld(void *obj, const char *name) {
    if (!obj) return NULL;
    for (void *k = il2cpp_object_get_class(obj); k; k = il2cpp_class_get_parent(k)) {
        void *f = il2cpp_class_get_field_from_name(k, name);
        if (f) return (char *)obj + il2cpp_field_get_offset(f);
    }
    return NULL;
}
static void *fld_obj(void *obj, const char *name) {
    void *p = fld(obj, name);
    return p ? *(void **)p : NULL;
}
static void *get_comp(void *component_or_tf, void *klass) {
    if (!component_or_tf || !klass || !m_GetComponent) return NULL;
    void *a[1] = { il2cpp_type_get_object(il2cpp_class_get_type(klass)) };
    return inv(m_GetComponent, component_or_tf, a);
}

/* dump the persistent listeners registered on a UnityEvent */
static void dump_unityevent(void *ev, const char *label) {
    if (!ev) { nlog("  %s: no event", label); return; }
    void *pcg = fld_obj(ev, "m_PersistentCalls");
    void *lst = pcg ? fld_obj(pcg, "m_Calls") : NULL;
    if (!lst) { nlog("  %s: no persistent call list (pcg=%p)", label, pcg); return; }
    int *sz = (int *)fld(lst, "_size");
    void *items = fld_obj(lst, "_items");
    nlog("  %s: %d persistent call(s)", label, sz ? *sz : -1);
    if (!sz || !items) return;
    void **el = (void **)((char *)items + 32);
    for (int i = 0; i < *sz; i++) {
        void *pc = el[i];
        if (!pc) continue;
        void *tgt = fld_obj(pc, "m_Target");
        void *mn  = fld_obj(pc, "m_MethodName");
        void *ac  = fld_obj(pc, "m_Arguments");
        int *mode = (int *)fld(pc, "m_Mode");
        char mname[96];
        cs_str(mn, mname, sizeof mname);
        int *ia = ac ? (int *)fld(ac, "m_IntArgument") : NULL;
        nlog("    call[%d] target=%s method='%s' mode=%d intArg=%d", i,
             tgt ? il2cpp_class_get_name(il2cpp_object_get_class(tgt)) : "(null)",
             mname, mode ? *mode : -1, ia ? *ia : -999);
    }
}


static void v2(void *boxed, float *x, float *y) {
    *x = *y = 0;
    if (!boxed) return;
    float *f = (float *)il2cpp_object_unbox(boxed);
    *x = f[0]; *y = f[1];
}

/* log a RectTransform's layout so the 4-player arrangement can be planned */
static void dump_rect(void *tf, const char *label) {
    if (!tf || !k_RectTransform) return;
    void *rt = get_comp(tf, k_RectTransform);
    if (!rt) { nlog("  %s: no RectTransform", label); return; }
    float amnx, amny, amxx, amxy, apx, apy, sdx, sdy, pvx, pvy, sx, sy;
    v2(inv(m_rt_anchorMin, rt, NULL), &amnx, &amny);
    v2(inv(m_rt_anchorMax, rt, NULL), &amxx, &amxy);
    v2(inv(m_rt_anchoredPos, rt, NULL), &apx, &apy);
    v2(inv(m_rt_sizeDelta, rt, NULL), &sdx, &sdy);
    v2(inv(m_rt_pivot, rt, NULL), &pvx, &pvy);
    v2(inv(m_localScale, rt, NULL), &sx, &sy);
    nlog("  %-22s anchor=(%.2f,%.2f)-(%.2f,%.2f) pos=(%.1f,%.1f) size=(%.1f,%.1f) pivot=(%.2f,%.2f) scale=(%.2f,%.2f)",
         label, amnx, amny, amxx, amxy, apx, apy, sdx, sdy, pvx, pvy, sx, sy);
}

typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;

static void place_panel(void *tf, float ax, float ay, float px, float py, float scale) {
    if (!tf) return;
    void *rt = get_comp(tf, k_RectTransform);
    if (!rt) return;
    V2 a = { ax, ay }, p = { px, py };
    V3 sc = { scale, scale, 1.0f };
    void *aa[1] = { &a };
    inv(m_rt_set_anchorMin, rt, aa);
    inv(m_rt_set_anchorMax, rt, aa);
    void *pa[1] = { &p };
    inv(m_rt_set_anchoredPos, rt, pa);
    void *sa[1] = { &sc };
    inv(m_set_localScale, rt, sa);
}

static void dump_components(void *go, const char *label) {
    if (!go || !m_GetComponents) return;
    void *targs[1] = { il2cpp_type_get_object(il2cpp_class_get_type(k_Component)) };
    void *arr = inv(m_GetComponents, go, targs);
    if (!arr) { nlog("no components on %s", label); return; }
    int n = il2cpp_array_length(arr);
    void **el = (void **)((char *)arr + 32);   /* il2cpp array data starts at +32 on 64-bit */
    nlog("--- components of %s (%d) ---", label, n);
    for (int i = 0; i < n; i++)
        if (el[i]) nlog("    %s", il2cpp_class_get_name(il2cpp_object_get_class(el[i])));
}

static int tf_children(void *tf);

#define MODE_FILE "/data/user/0/jp.konami.YugiohOcgSupports/files/neuronmod.mode"

static void save_mode(int m) {
    FILE *f = fopen(MODE_FILE, "w");
    if (!f) { nlog("could not write %s", MODE_FILE); return; }
    fprintf(f, "%d", m);
    fclose(f);
}
static int load_mode(void) {
    int m = -1;
    FILE *f = fopen(MODE_FILE, "r");
    if (f) { if (fscanf(f, "%d", &m) != 1) m = -1; fclose(f); }
    return m;
}

/* Our own screen modes sit above the game's three.  Mode 3 is four duelists and
   4 is five, both added earlier; 3 duelists came later, so it took the next free
   number rather than a lower one - the game's 0-2 are its own. */
static int mode_players(int mode) {
    if (mode == 3) return 4;
    if (mode == 4) return 5;
    if (mode == 5) return 3;
    return 2;
}
static int players_mode(int np) {
    if (np == 4) return 3;
    if (np == 5) return 4;
    if (np == 3) return 5;
    return 0;
}

static int read_calc_mode(void) {
    int v = 0;
    if (f_CalcMode && il2cpp_field_static_get_value) il2cpp_field_static_get_value(f_CalcMode, &v);
    return v;
}

static void write_calc_mode(int v) {
    if (f_CalcMode && il2cpp_field_static_set_value) il2cpp_field_static_set_value(f_CalcMode, &v);
}

static void *row_radio_image(void *rowTf) {
    void *radio = tf_child(rowTf, 1);          /* child 1 is "RadioButton" */
    return radio ? get_comp(radio, k_Image) : NULL;
}

/* paint the radio buttons so exactly `index` looks selected */
static void apply_selection(void *container, int index) {
    if (!container || !m_set_sprite || !g_onSprite || !g_offSprite) return;
    int n = tf_children(container), changed = 0;
    for (int i = 0; i < n; i++) {
        void *img = row_radio_image(tf_child(container, i));
        if (!img) continue;
        void *want = (i == index) ? g_onSprite : g_offSprite;
        if (inv(m_get_sprite, img, NULL) == want) continue;   /* already right */
        void *a[1] = { want };
        inv(m_set_sprite, img, a);
        changed++;
    }
    g_selMode = index;
    if (changed) nlog("selection repainted: row %d of %d (%d changed)", index, n, changed);
}

/* capture the on/off radio sprites from the stock rows */
static void capture_sprites(void *self, void *container) {
    if (g_onSprite && g_offSprite) return;
    int cur = read_calc_mode();
    if (cur > 2 || cur < 0) {
        /* Our own mode is active, so no stock row is lit and there is no "on"
           sprite to read.  Ask the game to paint row 0 first - with isSave = false
           so the stored mode is untouched - then learn both sprites from it. */
        if (m_OnClickCalcMode2 && self) {
            int zero = 0; uint8_t nosave = 0;
            void *a[2] = { &zero, &nosave };
            inv(m_OnClickCalcMode2, self, a);
            nlog("sprite capture: asked the game to paint row 0 (no save)");
        }
        cur = 0;
    }
    void *on  = row_radio_image(tf_child(container, cur));
    void *off = row_radio_image(tf_child(container, cur == 0 ? 1 : 0));
    if (on)  g_onSprite  = inv(m_get_sprite, on, NULL);
    if (off) g_offSprite = inv(m_get_sprite, off, NULL);
    nlog("sprites captured from mode %d: on=%p off=%p", cur, g_onSprite, g_offSprite);
}

/* point a cloned row's button at a different mode index */
static void retarget_button(void *rowTf, int mode) {
    void *btn = get_comp(rowTf, k_Button);
    if (!btn) { nlog("  clone has no Button"); return; }
    void *ev = fld_obj(btn, "m_OnClick");
    void *pcg = ev ? fld_obj(ev, "m_PersistentCalls") : NULL;
    void *lst = pcg ? fld_obj(pcg, "m_Calls") : NULL;
    int *sz = lst ? (int *)fld(lst, "_size") : NULL;
    void *items = lst ? fld_obj(lst, "_items") : NULL;
    if (!sz || !*sz || !items) { nlog("  clone button has no persistent call"); return; }
    void **el = (void **)((char *)items + 32);
    void *ac = fld_obj(el[0], "m_Arguments");
    int *ia = ac ? (int *)fld(ac, "m_IntArgument") : NULL;
    if (!ia) { nlog("  no int argument on clone call"); return; }
    *ia = mode;
    uint8_t *dirty = (uint8_t *)fld(ev, "m_CallsDirty");
    if (dirty) *dirty = 1;                  /* force the invokable list to rebuild */
    nlog("  button retargeted to mode %d (dirty=%d)", mode, dirty ? 1 : 0);
}

/* clone a row and relabel it */
static void *clone_row(void *srcGo, void *parentTf, const char *objname, const char *label, int mode) {
    void *args1[1];
    args1[0] = srcGo;
    void *clone = inv(m_Instantiate, NULL, args1);      /* static */
    if (!clone) { nlog("Instantiate failed for %s", label); return NULL; }
    void *ctf = inv(m_go_get_transform, clone, NULL);
    if (ctf) {
        uint8_t keep_world = 0;                 /* UI clones must not preserve world transform */
        void *sp[2] = { parentTf, &keep_world };
        inv(m_SetParent, ctf, sp);
    }
    args1[0] = il2cpp_string_new(objname);
    inv(m_set_name, clone, args1);
    /* first child is the label Text */
    void *txt = ctf ? tf_child(ctf, 0) : NULL;
    /* the clone inherits a BindingText localiser that would re-apply the source label */
    if (txt && k_BindingText && m_GetComponent) {
        void *ba[1] = { il2cpp_type_get_object(il2cpp_class_get_type(k_BindingText)) };
        void *binder = inv(m_GetComponent, txt, ba);
        nlog("  BindingText on label = %p", binder);
        if (binder) {
            if (m_set_enabled) { uint8_t off = 0; void *ea[1] = { &off }; inv(m_set_enabled, binder, ea); }
            if (m_Destroy)     { void *da[1] = { binder }; inv(m_Destroy, NULL, da); }
            nlog("  localiser removed");
        }
    }
    if (txt && k_TMP && m_set_text) {
        void *targs[1];
        targs[0] = il2cpp_type_get_object(il2cpp_class_get_type(k_TMP));
        void *comp = inv(m_GetComponent, txt, targs);
        if (comp) {
            targs[0] = il2cpp_string_new(label);
            inv(m_set_text, comp, targs);
            nlog("  labelled '%s'", label);
        } else {
            nlog("  no TMP_Text on label child of %s", objname);
        }
    }
    if (ctf) retarget_button(ctf, mode);
    nlog("cloned row %s (%s, mode %d) -> %p", objname, label, mode, clone);
    return clone;
}

static int tf_children(void *tf) {
    void *r = inv(m_get_childCount, tf, NULL);
    return r ? *(int *)il2cpp_object_unbox(r) : 0;
}
static void *tf_child(void *tf, int i) {
    void *args[1] = { &i };
    return inv(m_GetChild, tf, args);
}
static void tf_name(void *o, char *out, int n) {
    cs_str(inv(m_get_name, o, NULL), out, n);
}

static void dump_tree(void *tf, int depth, int maxdepth, const char *path) {
    char nm[128], pad[64], sub[256];
    int i, c = tf_children(tf);
    tf_name(tf, nm, sizeof nm);
    int p = depth * 2; if (p > 60) p = 60;
    memset(pad, ' ', p); pad[p] = 0;
    nlog("%s[%s] %s  (%d children)", pad, path, nm, c);
    if (depth >= maxdepth) return;
    for (i = 0; i < c; i++) {
        void *ch = tf_child(tf, i);
        if (!ch) continue;
        snprintf(sub, sizeof sub, "%s.%d", path, i);
        dump_tree(ch, depth + 1, maxdepth, sub);
    }
}

static void dump_fields(void *klass, const char *label) {
    void *iter = NULL, *f;
    nlog("--- fields of %s ---", label);
    while ((f = il2cpp_class_get_fields(klass, &iter))) {
        char *tn = il2cpp_type_get_name(il2cpp_field_get_type(f));
        nlog("  +%03zu %-28s : %s", il2cpp_field_get_offset(f), il2cpp_field_get_name(f), tn ? tn : "?");
    }
}

/* ---------- hook ---------- */
static void (*orig_OnEnable)(void *, void *);

static void (*orig_StartDuel_OnEnable)(void *, void *);
static int g_duel_dumped = 0;

/* find Duel/LifeArea under CalculatorMulti */
static void build_four_player_layout(void *self);
static void wire_panels(void);
static void dump_texts(void *tf, const char *path, int depth);
static int tf_visible(void *tf);
static void fix_keypad_header(void *self);

/* Each Calculator Design is its own prefab clone, and they do not all order
   their children the same way - looking the Duel node up by child index worked
   on some skins and silently found nothing on others, which left the mod
   doing nothing at all there.  Search by name instead. */
static void *find_deep(void *tf, const char *name, int depth) {
    if (!tf || depth < 0) return NULL;
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) {
        char nm[64]; tf_name(tf_child(tf, i), nm, sizeof nm);
        if (!strcmp(nm, name)) return tf_child(tf, i);
    }
    for (int i = 0; i < n; i++) {
        void *r = find_deep(tf_child(tf, i), name, depth - 1);
        if (r) return r;
    }
    return NULL;
}

static void *find_life_area(void *self) {
    void *multi = *(void **)((char *)self + 176);
    void *mtf = multi ? inv(m_go_get_transform, multi, NULL) : NULL;
    if (!mtf) return NULL;
    void *duel = find_deep(mtf, "Duel", 3);
    void *la = duel ? find_deep(duel, "LifeArea", 2) : NULL;
    if (!la) {
        static void *moaned;
        if (moaned != mtf) { moaned = mtf; nlog("4P: no LifeArea under this skin (duel=%p)", duel); }
    }
    return la;
}

/* set the "Duelist N" caption inside a cloned panel */
/* Skins nest the duelist caption differently and name the nodes differently, so
   neither a fixed path nor a name search finds it reliably.  But the two stock
   panels are the same prefab showing different text - so diff them.  The node
   whose text differs between Duelist 01 and Duelist 02 is the caption, and the
   child-index path to it works on every clone of either prefab. */

/* One-shot: what differs between the two stock panels?  Needed to find the
   duelist caption on skins where it is not TMP text. */
static void dump_panel_diff(void *a, void *b, int depth, const char *path) {
    if (!a || !b || depth < 0) return;
    char na[64];
    tf_name(a, na, sizeof na);
    char here[192];
    snprintf(here, sizeof here, "%s/%s", path, na);
    void *ca = k_TMP ? get_comp(a, k_TMP) : NULL;
    void *cb = k_TMP ? get_comp(b, k_TMP) : NULL;
    if (ca && cb && m_get_text) {
        char ta[80], tb[80];
        cs_str(inv(m_get_text, ca, NULL), ta, sizeof ta);
        cs_str(inv(m_get_text, cb, NULL), tb, sizeof tb);
        nlog("  TMP %s : '%s' | '%s'%s", here, ta, tb, strcmp(ta, tb) ? "   <-- DIFFERS" : "");
    }
    void *ia = k_Image ? get_comp(a, k_Image) : NULL;
    void *ib = k_Image ? get_comp(b, k_Image) : NULL;
    if (ia && ib && m_get_sprite) {
        void *sa = inv(m_get_sprite, ia, NULL), *sb = inv(m_get_sprite, ib, NULL);
        char nsa[80] = "(none)", nsb[80] = "(none)";
        if (sa) cs_str(inv(m_get_name, sa, NULL), nsa, sizeof nsa);
        if (sb) cs_str(inv(m_get_name, sb, NULL), nsb, sizeof nsb);
        if (strcmp(nsa, nsb)) nlog("  IMG %s : '%s' | '%s'   <-- DIFFERS", here, nsa, nsb);
    }
    int n = tf_children(a), m = tf_children(b);
    if (m < n) n = m;
    for (int i = 0; i < n; i++)
        dump_panel_diff(tf_child(a, i), tf_child(b, i), depth - 1, here);
}

#define CAPMAX 8
static int g_capPath[CAPMAX];
/* Names as well as paths: the paths go stale.  See panel_node(). */
#define NAMEPATH 256
static char g_capName[NAMEPATH], g_artName[NAMEPATH],
            g_txtName[NAMEPATH], g_difName[NAMEPATH];

/* "Duelist/TextPlayer/img" for a node len levels below the panel root. */
static void name_path_of(void *node, int len, char *out, size_t cap) {
    char seg[CAPMAX][64];
    void *t = node;
    out[0] = 0;
    if (len <= 0 || len > CAPMAX) return;
    for (int i = len - 1; i >= 0; i--) {
        if (!t) return;
        tf_name(t, seg[i], sizeof seg[i]);
        t = tf_parent(t);
    }
    for (int i = 0; i < len; i++) {
        size_t l = strlen(out);
        snprintf(out + l, cap - l, "%s%s", l ? "/" : "", seg[i]);
    }
}
static int g_capLen = -1;
static int g_capIsSprite;
static int g_artPath[CAPMAX];
static int g_artLen = -1;
static int g_txtPath[CAPMAX];
static int g_txtLen = -1;
static int g_difPath[CAPMAX];
static int g_difLen = -1;
static int g_difIsNumber;      /* written whenever g_difLen is */
static int g_capSaid;          /* diagnostic line budget, reset per design */

/* node names the skins use for the bit that carries the duelist's name */
#define NAMEMAX 40
static void *name_get(int which);

/* Is this the skin's own name field?  Empty on both panels, or holding the
   game's two duelist names - which is what it holds once either of them has
   been renamed, and testing only for empty lost the field at that point. */
static int field_is_free(const char *ta, const char *tb) {
    if (!ta[0] && !tb[0]) return 1;
    char g1[NAMEMAX] = "", g2[NAMEMAX] = "";
    void *s1 = name_get(0), *s2 = name_get(1);
    if (s1) cs_str(s1, g1, sizeof g1);
    if (s2) cs_str(s2, g2, sizeof g2);
    if (!g1[0] && !g2[0]) return 0;
    int aok = !ta[0] || (g1[0] && !strcmp(ta, g1)) || (g2[0] && !strcmp(ta, g2));
    int bok = !tb[0] || (g1[0] && !strcmp(tb, g1)) || (g2[0] && !strcmp(tb, g2));
    return aok && bok;
}

static int name_ish(const char *n) {
    return strstr(n, "Player") || strstr(n, "Name") || strstr(n, "Text");
}

static int diff_caption(void *a, void *b, int depth, int len) {
    if (!a || !b || depth < 0 || len > CAPMAX) return 0;
    void *ca = k_TMP ? get_comp(a, k_TMP) : NULL;
    void *cb = k_TMP ? get_comp(b, k_TMP) : NULL;
    if (ca && cb && m_get_text) {
        char ta[96], tb[96];
        cs_str(inv(m_get_text, ca, NULL), ta, sizeof ta);
        cs_str(inv(m_get_text, cb, NULL), tb, sizeof tb);
        if (ta[0] && strcmp(ta, tb)) {
            /* The caption is whatever text the two panels disagree about - which
               is fine until the two duelists have different life totals, and
               then the *score* disagrees too and gets taken for the caption.
               That is why coming into VRAINS straight from a duel where points
               had already been spent put the names where the numbers belong,
               while opening it on a fresh duel was fine.  A caption is never the
               life counter and never a bare number. */
            char nm[64];
            tf_name(a, nm, sizeof nm);
            /* Only the life counter is off limits.  Ruling out bare numbers as
               well went too far: 5D's writes its caption as a fixed 'DUELIST'
               with '01'/'02' underneath, and that number *is* the caption.
               Remember it rather than stopping here - the blank name field the
               skin would itself use for a custom name usually sits further along
               the walk, and it is the better place to put a name. */
            if (!strstr(nm, "Life") && g_difLen < 0) {
                for (int k = 0; k < len; k++) g_difPath[k] = g_capPath[k];
                g_difLen = len;
                name_path_of(a, len, g_difName, sizeof g_difName);
                /* Bare digits mean a caption split in two, the wording in the
                   parent and only the number here. */
                g_difIsNumber = 1;
                for (const char *q = ta; *q; q++)
                    if (!((*q >= '0' && *q <= '9') || *q == ' ')) { g_difIsNumber = 0; break; }
            }
        }
        /* Duel Monsters and VRAINS draw the caption as artwork and leave the
           name itself to a blank text field beside it - /Duelist/PlayerName,
           empty on both panels, so the differing-text search walked straight
           past it and no panel was ever captioned. */
        if (g_txtLen < 0 && field_is_free(ta, tb)) {
            char nm[64];
            tf_name(a, nm, sizeof nm);
            /* Only a field that is named for the duelist.  'Text' was too loose:
               on VRAINS the life counter is empty for the first few frames too,
               and its node matched, so the name was written where the number
               belongs and the panels showed no score at all.  Anything with
               'Life' in it is the score, never the name. */
            if ((strstr(nm, "Name") || strstr(nm, "Player")) && !strstr(nm, "Life")) {
                for (int k = 0; k < len; k++) g_txtPath[k] = g_capPath[k];
                g_txtLen = len;
                name_path_of(a, len, g_txtName, sizeof g_txtName);
                nlog("cap: blank name field '%s' at depth %d", nm, len);
            }
        }
    }
    /* Several skins draw the caption as artwork instead - and ship art only for
       players 1 and 2.  Spot it by the sprite differing, preferring a node whose
       name says it is the caption rather than the panel background. */
    void *ia = k_Image ? get_comp(a, k_Image) : NULL;
    void *ib = k_Image ? get_comp(b, k_Image) : NULL;
    if (ia && ib && m_get_sprite && g_artLen < 0) {
        char nm[64], pn[64] = "";
        tf_name(a, nm, sizeof nm);
        void *pa = tf_parent(a);
        if (pa) tf_name(pa, pn, sizeof pn);
        /* the artwork usually hangs one level under the node that is named for
           it - /Duelist/TextPlayer/img - so ask the parent as well */
        if (name_ish(nm) || name_ish(pn)) {
            void *sa = inv(m_get_sprite, ia, NULL), *sb = inv(m_get_sprite, ib, NULL);
            char x[80] = "", y[80] = "";
            if (sa) cs_str(inv(m_get_name, sa, NULL), x, sizeof x);
            if (sb) cs_str(inv(m_get_name, sb, NULL), y, sizeof y);
            if (x[0] && strcmp(x, y)) {
                /* remember it whether or not a text caption also turns up - some
                   skins carry both, and leaving the art on stacks two captions */
                for (int k = 0; k < len; k++) g_artPath[k] = g_capPath[k];
                g_artLen = len;
                name_path_of(a, len, g_artName, sizeof g_artName);
            }
        }
    }
    int n = tf_children(a), m = tf_children(b);
    if (m < n) n = m;
    for (int i = 0; i < n && len < CAPMAX; i++) {
        g_capPath[len] = i;
        if (diff_caption(tf_child(a, i), tf_child(b, i), depth - 1, len + 1)) return 1;
    }
    if (len == 0 && g_capLen < 0) {
        /* Order of preference: the caption the two panels disagree about, since
           that is where the skin itself puts a name and it is laid out for one;
           then the blank name field, for skins that draw their caption as art;
           then the art itself.
           Preferring the blank field instead was wrong: on Standard it sits
           behind the panel's marker, so the name was written somewhere you
           could not read it. */
        /* A split caption's number is the wrong half to write a name into: the
           parent keeps its wording and the two stack up, which is ZEXAL's
           'DUELIST' sitting on the name.  Swapping a number in place is fine,
           a name is not - take the blank name field instead when there is one. */
        if (g_difLen >= 0 && !(g_difIsNumber && g_txtLen >= 0)) {
            for (int k = 0; k < g_difLen; k++) g_capPath[k] = g_difPath[k];
            g_capLen = g_difLen; g_capIsSprite = 0;
            snprintf(g_capName, sizeof g_capName, "%s", g_difName); return 1;
        }
        if (g_txtLen >= 0) {
            for (int k = 0; k < g_txtLen; k++) g_capPath[k] = g_txtPath[k];
            g_capLen = g_txtLen; g_capIsSprite = 0;
            snprintf(g_capName, sizeof g_capName, "%s", g_txtName); return 1;
        }
        if (g_artLen >= 0) {
            for (int k = 0; k < g_artLen; k++) g_capPath[k] = g_artPath[k];
            g_capLen = g_artLen; g_capIsSprite = 1;
            snprintf(g_capName, sizeof g_capName, "%s", g_artName); return 1;
        }
    }
    return 0;
}

static void *node_path(void *root, const int *path, int len) {
    void *t = root;
    if (len < 0) return NULL;
    for (int i = 0; i < len && t; i++) {
        if (path[i] >= tf_children(t)) return NULL;
        t = tf_child(t, path[i]);
    }
    return t;
}
static void *node_at(void *root, int len) { return node_path(root, g_capPath, len); }

static void *find_child(void *tf, const char *name);

/* Resolve a node the search remembered, on one panel.  By its chain of names,
   not by the child index the search recorded: label_panel re-siblings the
   caption to the end of its parent, which renumbers every child after it.  The
   whole chain and not the leaf, because a leaf name need not be unique - Duel
   Monsters has both Duelist/Background/line/img and Duelist/TextPlayer/img. */
static void *panel_node(void *panelTf, const char *names, const int *path, int len) {
    if (!panelTf || len < 0) return NULL;
    void *t = panelTf;
    for (const char *p = names ? names : ""; *p && t; ) {
        char seg[64]; int k = 0;
        while (*p && *p != '/' && k < (int)sizeof seg - 1) seg[k++] = *p++;
        seg[k] = 0;
        if (*p == '/') p++;
        t = find_child(t, seg);
    }
    if (t && t != panelTf) return t;
    return node_path(panelTf, path, len);
}
static void *cap_node(void *panelTf) {
    if (g_capLen < 0) return NULL;
    return panel_node(panelTf, g_capName, g_capPath, g_capLen);
}


/* Is the caption search looking at a panel that has finished drawing itself?

   A design change wraps the new panels the moment they exist, and on some skins
   the life counter is still blank for a few frames after that.  Every empty text
   node then looks alike, so the search could settle on the wrong one and the
   name was written where the score belongs - which is why switching from ARC-V
   to VRAINS showed five names and no numbers, while opening VRAINS cold was
   fine.  Wait until the numbers are there before deciding anything. */
static int panel_ready(void *panelTf) {
    if (!panelTf || !m_get_text) return 0;
    void *lp = find_deep(panelTf, "LifePoints", 4);
    void *c  = lp ? get_comp(lp, k_TMP) : NULL;
    if (!c) return 1;                   /* skin without one - nothing to wait for */
    char t[32] = "";
    cs_str(inv(m_get_text, c, NULL), t, sizeof t);
    return t[0] != 0;
}

/* Never let the caption land on the score itself. */
static int caption_is_the_score(void *panelTf) {
    if (g_capLen < 0 || !panelTf) return 0;
    void *node = cap_node(panelTf);
    if (!node) return 0;
    void *lp = find_deep(panelTf, "LifePoints", 4);
    return node == lp;
}

static void strip_binder(void *tf) {
    if (!tf || !k_BindingText) return;
    void *ba[1] = { il2cpp_type_get_object(il2cpp_class_get_type(k_BindingText)) };
    void *binder = inv(m_GetComponent, tf, ba);
    if (!binder) return;
    if (m_set_enabled) { uint8_t off = 0; void *ea[1] = { &off }; inv(m_set_enabled, binder, ea); }
    if (m_Destroy) { void *da[1] = { binder }; inv(m_Destroy, NULL, da); }
}

static int set_tmp(void *tf, const char *text) {
    void *comp = tf ? get_comp(tf, k_TMP) : NULL;
    if (!comp || !m_set_text) return 0;
    strip_binder(tf);
    void *sa[1] = { il2cpp_string_new(text) };
    inv(m_set_text, comp, sa);
    return 1;
}

/* Skins stack the caption: a TMP with a second TMP under it for the shadow or
   outline, both holding the same string.  Setting only the top one left the old
   'DUELIST 01' showing through underneath - the doubled caption on ZEXAL. */
/* Write a caption without spraying it over the whole subtree.

   Skins stack a shadow copy under the caption that holds the same string, and
   that one has to be written too or the old text shows through.  But Standard
   keeps a second, narrower name field in there as well, and writing the name
   into that produced a clipped 'Vl' sitting beside the real 'Vlada'.  Only
   follow children that were saying exactly what the caption said. */
static void set_caption(void *tf, const char *text) {
    if (!tf) return;
    char cur[96] = "";
    void *c = get_comp(tf, k_TMP);
    if (c && m_get_text) cs_str(inv(m_get_text, c, NULL), cur, sizeof cur);
    set_tmp(tf, text);
    void *stack[16]; int sp = 0;
    for (int i = 0, n = tf_children(tf); i < n && sp < 16; i++) stack[sp++] = tf_child(tf, i);
    while (sp) {
        void *t = stack[--sp];
        void *tc = get_comp(t, k_TMP);
        if (tc && m_get_text) {
            char t2[96] = "";
            cs_str(inv(m_get_text, tc, NULL), t2, sizeof t2);
            /* a real shadow sits on top of the caption and is the same size;
               Standard's second field is much narrower, and writing the name
               there put a clipped copy beside the real one */
            float w1 = 0, h1 = 0, w2 = 0, h2 = 0;
            rect_size(tf, &w1, &h1);
            rect_size(t, &w2, &h2);
            int sameBox = (w1 > 1.0f && w2 > w1 * 0.9f && w2 < w1 * 1.1f);
            if (cur[0] && !strcmp(t2, cur) && sameBox) set_tmp(t, text);
            {
                static int said;
                if (said < 12) { said++;
                    char n1[64], n2[64];
                    tf_name(tf, n1, sizeof n1); tf_name(t, n2, sizeof n2);
                    nlog("cap child %s/%s '%s' w %.0f vs %.0f -> %s",
                         n1, n2, t2, w2, w1, (cur[0] && !strcmp(t2, cur) && sameBox) ? "write" : "skip");
                }
            }
        }
        for (int i = 0, n = tf_children(t); i < n && sp < 16; i++) stack[sp++] = tf_child(t, i);
    }
}

static void set_tmp_tree(void *tf, const char *text, int depth) {
    if (!tf || depth < 0) return;
    set_tmp(tf, text);
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) set_tmp_tree(tf_child(tf, i), text, depth - 1);
}

/* Build a clone's caption out of the one the skin already shows.

   The skins do not write the caption the same way: ZEXAL splits it into a
   fixed 'DUELIST' with a separate '01' underneath, which is the node the diff
   finds - so writing 'Duelist 3' into it produced 'DUELISTDuelist 3' stacked on
   itself.  Swapping just the number keeps each skin's own wording and padding. */
static void caption_from(const char *src, int n, char *out, size_t cap) {
    int start = -1, end = -1;
    for (int i = (int)strlen(src) - 1; i >= 0; i--) {
        if (src[i] >= '0' && src[i] <= '9') { if (end < 0) end = i; start = i; }
        else if (end >= 0) break;
    }
    if (end < 0) { snprintf(out, cap, "Duelist %d", n); return; }
    snprintf(out, cap, "%.*s%0*d%s", start, src, end - start + 1, n, src + end + 1);
}


/* ---------- duelist names ----------

   The game holds exactly two: static DuelistName1/DuelistName2 on StartDuel,
   mirrored into the LogArchive, with SetDuelistName1/2 and IsSetDuelistName1/2
   either side.  Nothing in it is indexed by player, so a name typed for anyone
   past the second was written into duelist 2's slot or thrown away - which is
   why a new name never survived the OK button.

   So keep our own five, write them into the panels ourselves, and put the
   game's own two back the way they were whenever the rename was not for them. */
#define NAME_FILE "/data/user/0/jp.konami.YugiohOcgSupports/files/neuronmod.names"
static char g_pname[5][NAMEMAX];
static int  g_nameDirty;

static void save_names(void) {
    FILE *f = fopen(NAME_FILE, "w");
    if (!f) { nlog("names: cannot write %s", NAME_FILE); return; }
    for (int i = 0; i < 5; i++) fprintf(f, "%s\n", g_pname[i]);
    fclose(f);
    nlog("names: saved '%s' '%s' '%s' '%s' '%s'",
         g_pname[0], g_pname[1], g_pname[2], g_pname[3], g_pname[4]);
}

static void load_names(void) {
    FILE *f = fopen(NAME_FILE, "r");
    if (!f) return;
    for (int i = 0; i < 5; i++) {
        if (!fgets(g_pname[i], NAMEMAX, f)) break;
        char *nl = strchr(g_pname[i], '\n');
        if (nl) *nl = 0;
    }
    fclose(f);
    nlog("names: loaded '%s' '%s' '%s' '%s' '%s'",
         g_pname[0], g_pname[1], g_pname[2], g_pname[3], g_pname[4]);
}

/* the game's own two, so they can be put back after a rename meant for someone else */
static void *name_field(int which) {
    static void *f1, *f2;
    if (!f1) {
        void *k = cls(g_img_cs, "", "StartDuel");
        if (!k) return NULL;
        f1 = il2cpp_class_get_field_from_name(k, "DuelistName1");
        f2 = il2cpp_class_get_field_from_name(k, "DuelistName2");
    }
    return which ? f2 : f1;
}

static void *name_get(int which) {
    void *f = name_field(which), *v = NULL;
    if (f) il2cpp_field_static_get_value(f, &v);
    return v;
}

static void name_set(int which, void *strObj) {
    void *f = name_field(which);
    if (f) il2cpp_field_static_set_value(f, strObj);
}

/* Catching the rename.

   The name is typed into a TMP_InputField and confirmed with the keyboard's own
   OK - and StartDuel.OnDuelistnameSubmit, which is where that should land, never
   runs through the patched entry, the same way the '=' key does not.  So watch
   the field instead.

   The field keeps whatever was typed into it last, for everyone.  Watching it
   naively therefore handed the previous duelist's name to the next one you
   opened - rename duelist 2 to Aleksa and duelists 3, 4 and 5 all became Aleksa
   as soon as you looked at them.  So the field is loaded with the selected
   duelist's own name every time the selection changes, and a name is only
   committed when what is in the field actually differs from what we put there. */
static void watch_rename(void *self) {
    if (g_selMode < 3) return;
    void *inf = *(void **)((char *)self + 256);      /* DuelistNameIf */
    if (!inf) return;
    static void *kif, *mText, *mSetText, *mFocus;
    void *k = il2cpp_object_get_class(inf);
    if (k != kif) {
        kif = k;
        mText    = il2cpp_class_get_method_from_name(k, "get_text", 0);
        mSetText = il2cpp_class_get_method_from_name(k, "set_text", 1);
        mFocus   = il2cpp_class_get_method_from_name(k, "get_isFocused", 0);
        nlog("rename: field get=%p set=%p focus=%p", mText, mSetText, mFocus);
    }
    if (!mText || !mFocus) return;

    int idx = *(int *)((char *)self + 44);
    static int  lastIdx = -1;
    static char baseline[NAMEMAX];
    static char pending[NAMEMAX];
    static int  wasFocused;
    if (idx != lastIdx) {
        lastIdx = idx;
        wasFocused = 0;
        pending[0] = 0;
        const char *want = (idx >= 0 && idx < 5) ? g_pname[idx] : "";
        snprintf(baseline, sizeof baseline, "%s", want);
        if (mSetText) {
            void *sa[1] = { il2cpp_string_new(want) };
            inv(mSetText, inf, sa);
        }
        return;
    }

    void *rf = inv(mFocus, inf, NULL);
    int focused = rf ? *(uint8_t *)il2cpp_object_unbox(rf) : 0;
    if (focused) {
        char cur[NAMEMAX] = "";
        cs_str(inv(mText, inf, NULL), cur, sizeof cur);
        if (cur[0]) snprintf(pending, sizeof pending, "%s", cur);
        wasFocused = 1;
        return;
    }
    if (!wasFocused) return;
    wasFocused = 0;
    if (!pending[0] || idx < 0 || idx >= 5 || !strcmp(pending, baseline)) {
        pending[0] = 0;
        return;
    }
    snprintf(g_pname[idx], NAMEMAX, "%s", pending);
    snprintf(baseline, sizeof baseline, "%s", pending);
    pending[0] = 0;
    save_names();
    g_nameDirty = 1;
    nlog("name: duelist %d is now '%s'", idx + 1, g_pname[idx]);
}

static void (*orig_NameSubmit)(void *, void *, void *);
static void my_NameSubmit(void *self, void *str, void *mi) {
    int idx = (g_selMode >= 3) ? *(int *)((char *)self + 44) : -1;
    void *keep1 = NULL, *keep2 = NULL;
    if (idx >= 2) { keep1 = name_get(0); keep2 = name_get(1); }
    if (idx >= 0 && idx < 5) {
        char buf[NAMEMAX] = "";
        if (str) cs_str(str, buf, sizeof buf);
        snprintf(g_pname[idx], NAMEMAX, "%s", buf);
        save_names();
        g_nameDirty = 1;
        nlog("name: duelist %d is now '%s'", idx + 1, g_pname[idx]);
    }
    if (orig_NameSubmit) orig_NameSubmit(self, str, mi);
    if (idx >= 2) { name_set(0, keep1); name_set(1, keep2); }
}

static void (*orig_ResetNames)(void *, void *);
static void my_ResetNames(void *self, void *mi) {
    if (orig_ResetNames) orig_ResetNames(self, mi);
    for (int i = 0; i < 5; i++) g_pname[i][0] = 0;
    save_names();
    g_nameDirty = 1;
    nlog("name: all five reset");
}

static int drawn_box(void *tf, float *cx, float *cy, float *w, float *h);

static void rect_set(void *rt, float amnx, float amny, float amxx, float amxy,
                     float pvx, float pvy, float w, float h, float px, float py);
static int world_centre(void *tf, float *cx, float *cy);

/* Put one rect exactly where another one is. */
static void copy_rect(void *dst, void *src) {
    void *a = dst ? get_comp(dst, k_RectTransform) : NULL;
    void *b = src ? get_comp(src, k_RectTransform) : NULL;
    if (!a || !b || !m_rt_anchorMin || !m_rt_set_anchorMin) return;
    float mnx, mny, mxx, mxy, pvx, pvy, sdx, sdy, apx, apy;
    v2(inv(m_rt_anchorMin, b, NULL), &mnx, &mny);
    v2(inv(m_rt_anchorMax, b, NULL), &mxx, &mxy);
    v2(inv(m_rt_pivot, b, NULL), &pvx, &pvy);
    v2(inv(m_rt_sizeDelta, b, NULL), &sdx, &sdy);
    v2(inv(m_rt_anchoredPos, b, NULL), &apx, &apy);
    rect_set(a, mnx, mny, mxx, mxy, pvx, pvy, sdx, sdy, apx, apy);
}

static void label_panel(void *panelTf, const char *text) {
    if (!panelTf || g_capLen < 0) return;
    void *node = cap_node(panelTf);
    if (!node) return;
    if (!g_capIsSprite) {
        set_active(node, 1);           /* the blank name field ships switched off */
        set_caption(node, text);
        /* Where the skin draws its own caption - noted before it is hidden, and
           used at the end to move ours onto the same spot.

           VRAINS anchors PlayerName so its box hangs off the right of the
           panel.  At full size that is harmless; our panels are smaller, and
           the glyphs, right-aligned inside that box, landed off the plate and
           were clipped.  Everything about the field measured fine, which is why
           this took so long to see - it was being drawn, just nowhere you could
           look at it. */
        float stockX = 0, stockY = 0;
        int haveStock = 0;
        {
            void *stock = NULL;
            if (g_difLen >= 0) stock = panel_node(panelTf, g_difName, g_difPath, g_difLen);
            if (!stock && g_artLen >= 0) stock = panel_node(panelTf, g_artName, g_artPath, g_artLen);
            if (stock && stock != node) haveStock = world_centre(stock, &stockX, &stockY);
        }
        {   /* A caption that measures nothing has a font it cannot draw with.

               VRAINS' PlayerName is active, opaque, the right size and in the
               right place, and holds the name - and shows nothing, because the
               font it was built with is not in this trimmed-down APK any more.
               There is no point guessing: ask TMP how wide the string comes out,
               and if it comes out as nothing, borrow the font the panel is
               already drawing its score with. */
            void *c = get_comp(node, k_TMP);
            int broken = 0;
            if (c && text[0]) {
                void *r = m_tmp_prefW ? inv(m_tmp_prefW, c, NULL) : NULL;
                if (r && *(float *)il2cpp_object_unbox(r) < 1.0f) broken = 1;
                /* TMP measures a string from the font's metrics, which survive
                   even when the atlas texture behind them does not - so a
                   caption can measure perfectly and still draw nothing.  Ask the
                   material whether it has a texture at all. */
                void *mf = fld(c, "m_sharedMaterial");
                void *mat = mf ? *(void **)mf : NULL;
                void *kMat = g_img_core ? cls(g_img_core, "UnityEngine", "Material") : NULL;
                void *mTex = kMat ? meth(kMat, "get_mainTexture", 0) : NULL;
                if (mat && mTex && !inv(mTex, mat, NULL)) broken = 1;
                if (!mat) broken = 1;
            }
            if (broken) {
                void *lp = find_deep(panelTf, "LifePoints", 4);
                void *lc = lp ? get_comp(lp, k_TMP) : NULL;
                if (lc) {
                    void *fa = fld(lc, "m_fontAsset"), *fb = fld(c, "m_fontAsset");
                    void *ma = fld(lc, "m_sharedMaterial"), *mb = fld(c, "m_sharedMaterial");
                    if (fa && fb) *(void **)fb = *(void **)fa;
                    if (ma && mb) *(void **)mb = *(void **)ma;
                    set_tmp(node, text);
                    nlog("label: borrowed the score's font - the caption's own draws nothing");
                } else {
                    nlog("label: caption font is unusable and there is nothing to borrow");
                }
            }
        }
        {   /* Lift a caption whose own box is a sliver of the panel.

               Not "small against the score": that caught Simple, whose caption
               box is 50 of a 296 panel and reads fine, and set 79.8pt type in
               it - half again taller than the box, and it stopped drawing at
               all.  The box against the panel separates them: ZEXAL 25/400 and
               VRAINS 25/400 against Simple 50/296, Standard 50/540, Duel
               Monsters 110/420, ARC-V 50/400. */
            void *lp = find_deep(panelTf, "LifePoints", 4);
            void *lc = lp ? get_comp(lp, k_TMP) : NULL;
            void *cc = get_comp(node, k_TMP);
            float boxW = 0, boxH = 0, panW = 0, panH = 0;
            rect_size(node, &boxW, &boxH);
            rect_size(panelTf, &panW, &panH);
            if (lc && cc && m_tmp_getFs && m_tmp_setFs && m_tmp_autosize
                && panH > 1.0f && boxH > 1.0f && boxH < panH * 0.08f) {
                void *rl = inv(m_tmp_getFs, lc, NULL);
                void *rc = inv(m_tmp_getFs, cc, NULL);
                if (rl && rc) {
                    float lpfs = *(float *)il2cpp_object_unbox(rl);
                    float cfs  = *(float *)il2cpp_object_unbox(rc);
                    if (lpfs > 1.0f && cfs > 0.0f && cfs < lpfs * 0.30f) {
                        float want = lpfs * 0.38f;
                        uint8_t off = 0;              /* auto-sizing would undo this */
                        void *a[1] = { &off };
                        inv(m_tmp_autosize, cc, a);
                        a[0] = &want; inv(m_tmp_setFs, cc, a);
                        if (g_capSaid < 20) { g_capSaid++;
                            nlog("cap size: %.1f -> %.1f (score %.1f, box %.0f of panel %.0f)",
                                 cfs, want, lpfs, boxH, panH); }
                    }
                }
            }
        }
        {   /* Draw it last.  On VRAINS the name field sits earlier in the
               parent than the bar it belongs to, so the bar was painted over
               the name: the field was active, opaque and in the right place,
               and still nothing showed. */
            void *par = tf_parent(node);
            int n = par ? tf_children(par) : 0;
            if (n > 1 && tf_child(par, n - 1) != node) set_sibling(node, n - 1);
        }
        /* ZEXAL splits the caption in two: a fixed DUELIST in the parent with
           just the number underneath it, which is the half the diff finds.  A
           number can be swapped in place, but a name cannot - it came out as
           'DUELIST Vlada' - so the fixed half goes when we write a name. */
        if (g_capLen >= 1) {
            void *par = tf_parent(node);
            if (par && get_comp(par, k_TMP) && par != panelTf) {
                char cur[64] = "";
                if (m_get_text) cs_str(inv(m_get_text, get_comp(par, k_TMP), NULL), cur, sizeof cur);
                int digits = 1;
                for (const char *p = cur; *p; p++)
                    if (!((*p >= '0' && *p <= '9') || *p == ' ')) { digits = 0; break; }
                if (!digits && cur[0]) set_tmp(par, "");
            }
        }
        {   /* The game paints its own two duelist names onto the panels from
               StartDuel.DuelistName1/2, into a narrow field of its own, and the
               clones inherit it - so every panel carried a clipped second copy
               ('Vl' beside 'Vlada').  Wipe any text in this panel that is one of
               the game's two names, except the caption we just wrote. */
            char g1[NAMEMAX] = "", g2[NAMEMAX] = "";
            void *s1 = name_get(0), *s2 = name_get(1);
            if (s1) cs_str(s1, g1, sizeof g1);
            if (s2) cs_str(s2, g2, sizeof g2);
            if (g1[0] || g2[0]) {
                void *stack[24]; int sp = 0; stack[sp++] = panelTf;
                while (sp) {
                    void *t = stack[--sp];
                    if (t != node) {
                        void *tc = get_comp(t, k_TMP);
                        if (tc && m_get_text) {
                            char cur2[96] = "";
                            cs_str(inv(m_get_text, tc, NULL), cur2, sizeof cur2);
                            if (cur2[0] && ((g1[0] && !strcmp(cur2, g1)) ||
                                            (g2[0] && !strcmp(cur2, g2))))
                                set_tmp(t, "");
                        }
                    }
                    for (int i = 0, n = tf_children(t); i < n && sp < 24; i++)
                        stack[sp++] = tf_child(t, i);
                }
            }
        }
        if (g_txtLen >= 0) {   /* the skin's blank name field - the game fills it
                                  from its own two duelist names, and on Standard
                                  that came out as a clipped second copy beside
                                  ours */
            void *other = panel_node(panelTf, g_txtName, g_txtPath, g_txtLen);
            if (other && other != node) set_tmp_tree(other, "", 2);
        }
        if (g_difLen >= 0) {   /* skin also has its own caption text - clear it,
                                  or the stock 'DUELIST 01' sits over the name */
            void *dif = panel_node(panelTf, g_difName, g_difPath, g_difLen);
            if (dif && dif != node) {
                set_tmp_tree(dif, "", 2);
                void *par = tf_parent(dif);
                if (par && par != panelTf && par != node && get_comp(par, k_TMP))
                    set_tmp(par, "");
            }
        }
        if (g_artLen >= 0) {                     /* skin carries both - drop the art */
            void *art = panel_node(panelTf, g_artName, g_artPath, g_artLen);
            /* The artwork is usually one sprite inside a node named for the
               caption - /Duelist/TextPlayer/01 on VRAINS - and hiding only the
               sprite left the fixed 'DUELIST' word sitting over the name field.
               Hide the whole caption node. */
            if (art && art != node) {
                void *par = tf_parent(art);
                char pn[64] = "";
                if (par) tf_name(par, pn, sizeof pn);
                if (par && par != panelTf && par != node && name_ish(pn)) art = par;
                set_active(art, 0);
            }
        }
        if (haveStock) {
            /* Horizontally only - the nudge is for VRAINS, which anchors its
               name box off the right of the plate.  Matching the y as well
               parked the name on the hairline the skins rule their caption off
               with (Standard's is 7.78 x 0.04) and struck every name through. */
            float cx, cy, wx, wy, wz;
            if (world_centre(node, &cx, &cy) && world_pos(node, &wx, &wy, &wz))
                set_world(node, wx + (stockX - cx), wy, wz);
        }
        {   /* Diagnostics only.  A screenshot cannot separate "off the plate"
               from "too small to see" from "written into a node the skin never
               draws", so print all three.  Capped per design, since this runs
               every frame and a design change has to get its own lines. */
            if (g_capSaid < 20) { g_capSaid++;
                char nm[64] = "?";
                float w = 0, h = 0, wcx = 0, wcy = 0, cfs = 0, lpfs = 0;
                tf_name(node, nm, sizeof nm);
                rect_size(node, &w, &h);
                world_centre(node, &wcx, &wcy);
                void *cc = get_comp(node, k_TMP);
                void *lp = find_deep(panelTf, "LifePoints", 4);
                void *lc = lp ? get_comp(lp, k_TMP) : NULL;
                if (cc && m_tmp_getFs) {
                    void *r = inv(m_tmp_getFs, cc, NULL);
                    if (r) cfs = *(float *)il2cpp_object_unbox(r);
                }
                if (lc && m_tmp_getFs) {
                    void *r = inv(m_tmp_getFs, lc, NULL);
                    if (r) lpfs = *(float *)il2cpp_object_unbox(r);
                }
                nlog("cap where: '%s' rect %.2fx%.2f at %.2f,%.2f fs %.1f vs score %.1f text '%s'",
                     nm, w, h, wcx, wcy, cfs, lpfs, text);
            }
        }
        {   /* one-shot: what else on this plate is showing text? */
            static int said;
            if (!said) {
                said = 1;
                void *stack[24]; int sp = 0; stack[sp++] = panelTf;
                while (sp) {
                    void *t = stack[--sp];
                    void *tc = get_comp(t, k_TMP);
                    if (tc && m_get_text) {
                        char nm2[64], t2[96] = "";
                        tf_name(t, nm2, sizeof nm2);
                        cs_str(inv(m_get_text, tc, NULL), t2, sizeof t2);
                        float w = 0, h = 0; rect_size(t, &w, &h);
                        nlog("plate %-22s '%s' w=%.0f %s", nm2, t2, w, t == node ? "<= ours" : "");
                    }
                    for (int i = 0, n = tf_children(t); i < n && sp < 24; i++)
                        stack[sp++] = tf_child(t, i);
                }
            }
        }
        return;
    }
    /* The caption is artwork with no art for players 3-5, so hide it and write
       into the empty player-name text that sits beside it. */
    void *parent = tf_parent(node);
    int n = parent ? tf_children(parent) : 0;
    /* Take the *empty* text node beside it - that is the player-name field the
       skin leaves blank.  The other one holds the "LP" heading. */
    for (int i = 0; i < n; i++) {
        void *sib = tf_child(parent, i);
        if (sib == node) continue;
        void *comp = get_comp(sib, k_TMP);
        if (!comp || !m_get_text) continue;
        char cur[64];
        cs_str(inv(m_get_text, comp, NULL), cur, sizeof cur);
        if (cur[0]) continue;                    /* already says something */
        if (set_tmp(sib, text)) {
            set_tmp_tree(sib, text, 3);
            /* the skin leaves this field switched off and sized for a custom
               name, so wake it up and give it the artwork's slot */
            set_active(sib, 1);      /* the skin ships it switched off */
            /* borrow the font from the sibling that already shows skin-styled
               text (the "LP" heading), so 3-5 do not stand out */
            for (int j = 0; j < n; j++) {
                void *don = tf_child(parent, j);
                if (don == sib || don == node) continue;
                void *dc = get_comp(don, k_TMP);
                void *sc = get_comp(sib, k_TMP);
                if (!dc || !sc) continue;
                void *fa = fld(dc, "m_fontAsset");
                void *fb = fld(sc, "m_fontAsset");
                if (fa && fb) *(void **)fb = *(void **)fa;
                void *ma = fld(dc, "m_sharedMaterial"), *mb = fld(sc, "m_sharedMaterial");
                if (ma && mb) *(void **)mb = *(void **)ma;
                break;
            }
            set_tmp(sib, text);
            set_active(node, 0);
            nlog("label: captioned '%s' into the blank name field", text);
            return;
        }
    }
    nlog("label: caption is artwork and no blank text node beside it");
}

static void *make_wrapper(void *parentTf, const char *name, float dx, float dy, float scale);

static void *find_child(void *tf, const char *name) {
    if (!tf) return NULL;
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) {
        void *c = tf_child(tf, i);
        char nm[64]; tf_name(c, nm, sizeof nm);
        if (!strcmp(nm, name)) return c;
    }
    return NULL;
}

static void set_active(void *tf, int on) {
    if (!tf || !m_SetActive) return;
    void *go = inv(m_get_gameObject, tf, NULL);
    if (!go) return;
    uint8_t v = on ? 1 : 0;
    void *a[1] = { &v };
    inv(m_SetActive, go, a);
}

static void get_anchored(void *tf, float *x, float *y) {
    *x = *y = 0;
    void *rt = get_comp(tf, k_RectTransform);
    if (rt) v2(inv(m_rt_anchoredPos, rt, NULL), x, y);
}

/* a reparented element only makes sense once it uses its new neighbours' anchors */
static void copy_anchors(void *src, void *dst) {
    void *s = get_comp(src, k_RectTransform), *d = get_comp(dst, k_RectTransform);
    if (!s || !d) return;
    float ax, ay, bx, by;
    v2(inv(m_rt_anchorMin, s, NULL), &ax, &ay);
    v2(inv(m_rt_anchorMax, s, NULL), &bx, &by);
    V2 mn = { ax, ay }, mx = { bx, by };
    void *a[1] = { &mn }, *b[1] = { &mx };
    inv(m_rt_set_anchorMin, d, a);
    inv(m_rt_set_anchorMax, d, b);
}

static void set_anchored(void *tf, float x, float y) {
    void *rt = get_comp(tf, k_RectTransform);
    if (!rt) return;
    V2 p = { x, y };
    void *a[1] = { &p };
    inv(m_rt_set_anchoredPos, rt, a);
}

/* ---- the button row ----------------------------------------------------
   Unity does have the WPF-style answer: a RectTransform anchored to the bottom
   edge with a HorizontalLayoutGroup on it hands out the width itself, so no
   button is ever placed by pixel.  The catch is that the buttons' own
   RectTransforms are rewritten by game code every frame, so each one sits three
   levels deep and every level has exactly one owner:

     ModButtonRow    - ours: stretched across the bottom of the duel screen
       ModCell<name> - the layout group's: one quarter of the row
         ModHold<..> - ours: cancels whatever offset the game keeps writing
           <button>  - still the game's, and that is fine

   Nothing we own is touched by the game, so the row survives every frame.     */

#define NBTN 4
static void *k_HLG;
static void *g_row, *g_cell[NBTN], *g_hold[NBTN], *g_btn[NBTN], *g_ref[NBTN];
static void *g_btnHome[NBTN];      /* where each button lived before we took it */
static int   g_btnHomeIdx[NBTN];
static int   g_quitHomeIdx = -1;
static float g_lastW, g_lastH;
static void *g_slot[5], *g_phold[5], *g_panel[5];
static int   g_np;
static int   g_psettle;
static int   g_labelled;
static void *g_duelNode;
static float g_timerFix, g_rowFix;
static int glass_centre_world(float *out);
static float g_drawnW, g_drawnH;
static int g_dumpGfx;
static float g_quitY;
static int   g_stableN;
static int   g_settle;
static const char *const g_btnName[NBTN] = { "Log", "Reset", "Undo", "Tools" };

static void *new_rect(void *parentTf, const char *name);
static void rect_set(void *rt, float amnx, float amny, float amxx, float amxy,
                     float pvx, float pvy, float w, float h, float px, float py);




static void rect_size(void *tf, float *w, float *h) {
    *w = *h = 0;
    void *rt = get_comp(tf, k_RectTransform);
    if (!rt || !m_rt_rect) return;
    void *r = inv(m_rt_rect, rt, NULL);
    if (!r) return;
    float *f = (float *)il2cpp_object_unbox(r);
    *w = f[2]; *h = f[3];
}
static int world_pos(void *tf, float *x, float *y, float *z) {
    void *p = tf ? inv(m_get_position, tf, NULL) : NULL;
    if (!p) return 0;
    float *f = (float *)il2cpp_object_unbox(p);
    *x = f[0]; *y = f[1]; *z = f[2];
    return 1;
}
static void set_world(void *tf, float x, float y, float z) {
    V3 p = { x, y, z };
    void *a[1] = { &p };
    inv(m_set_position, tf, a);
}

/* Transform.position is the *pivot*, and the skins do not all pivot their panels
   in the middle - which is why some designs came out off-centre.  Take the rect's
   world corners instead, so the visible box is what gets aligned. */
static int world_centre(void *tf, float *cx, float *cy) {
    if (!tf) return 0;
    void *rt = get_comp(tf, k_RectTransform);
    if (rt && m_GetWorldCorners && il2cpp_array_new && k_Vector3) {
        void *arr = il2cpp_array_new(k_Vector3, 4);
        if (arr) {
            void *a[1] = { arr };
            inv(m_GetWorldCorners, rt, a);
            float *f = (float *)((char *)arr + 32);
            *cx = (f[0] + f[6]) * 0.5f;
            *cy = (f[1] + f[7]) * 0.5f;
            return 1;
        }
    }
    float z;
    return world_pos(tf, cx, cy, &z);
}

/* What a subtree actually draws, in world units.

   A skin's panel rect is not the plate it puts on screen: ARC-V's shield art
   bleeds well outside its rect, and 5D's ring sits off-centre inside one.  The
   rect is what the game lays out with, but the pixels are what the eye judges,
   so measure the union of every graphic that is really visible and align on
   that instead.  Hidden nodes are skipped - a caption we switched off must not
   drag the box sideways. */
static int g_duNodes, g_duGfx, g_duHidden;
static void drawn_union(void *tf, int depth, float *b, int *found) {
    if (!tf || depth < 0) return;
    g_duNodes++;
    if (m_activeInHierarchy) {
        void *go = inv(m_get_gameObject, tf, NULL);
        void *r  = go ? inv(m_activeInHierarchy, go, NULL) : NULL;
        if (r && !*(uint8_t *)il2cpp_object_unbox(r)) { g_duHidden++; return; }
    }
    {   /* 'btn' is the transparent touch area.  It is wider than the plate and
           sits off to one side, so counting it as drawn content dragged the
           middle window about 50px sideways on ZEXAL and VRAINS. */
        char nm[64]; tf_name(tf, nm, sizeof nm);
        if (!strcmp(nm, "btn")) return;
    }
    if ((k_Image && get_comp(tf, k_Image)) || (k_TMP && get_comp(tf, k_TMP))) {
        g_duGfx++;
        void *rt = get_comp(tf, k_RectTransform);
        if (rt && m_GetWorldCorners && il2cpp_array_new && k_Vector3) {
            void *arr = il2cpp_array_new(k_Vector3, 4);
            if (arr) {
                void *a[1] = { arr };
                inv(m_GetWorldCorners, rt, a);
                float *f = (float *)((char *)arr + 32);
                for (int i = 0; i < 4; i++) {
                    float x = f[i * 3], y = f[i * 3 + 1];
                    if (!*found) { b[0] = b[2] = x; b[1] = b[3] = y; *found = 1; }
                    if (x < b[0]) b[0] = x;
                    if (y < b[1]) b[1] = y;
                    if (x > b[2]) b[2] = x;
                    if (y > b[3]) b[3] = y;
                }
            }
        }
    }
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) drawn_union(tf_child(tf, i), depth - 1, b, found);
}
static int g_dbgDrawn;

/* One-shot inventory of everything a panel draws: name, size and where it sits.
   Needed to tell a skin's solid plate from the glow around it - the union box
   alone cannot separate the two. */
static void dump_graphics(void *tf, int depth, const char *path) {
    if (!tf || depth > 5 || !g_dumpGfx) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    char here[192];
    snprintf(here, sizeof here, "%s/%s", path, nm);
    if ((k_Image && get_comp(tf, k_Image)) || (k_TMP && get_comp(tf, k_TMP))) {
        float w = 0, h = 0, cx = 0, cy = 0;
        {   /* world box, so sizes are comparable across a skin's nested scales */
            void *rt = get_comp(tf, k_RectTransform);
            void *arr = (rt && k_Vector3 && il2cpp_array_new) ? il2cpp_array_new(k_Vector3, 4) : NULL;
            if (arr) {
                void *a[1] = { arr };
                inv(m_GetWorldCorners, rt, a);
                float *f = (float *)((char *)arr + 32);
                w = f[6] - f[0]; h = f[7] - f[1];
                cx = (f[0] + f[6]) * 0.5f; cy = (f[1] + f[7]) * 0.5f;
            }
        }
        int hidden = 0;
        if (m_activeInHierarchy) {
            void *go = inv(m_get_gameObject, tf, NULL);
            void *r  = go ? inv(m_activeInHierarchy, go, NULL) : NULL;
            if (r && !*(uint8_t *)il2cpp_object_unbox(r)) hidden = 1;
        }
        nlog("gfx: %-40s %6.2fx%-6.2f x=%6.2f %s", here, w, h, cx,
             hidden ? "hidden" : ((k_Image && get_comp(tf, k_Image)) ? "img" : "text"));
    }
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) dump_graphics(tf_child(tf, i), depth + 1, here);
}

/* Every visible graphic in a panel, as a world box. */
#define MAXBOX 48
typedef struct { float w, h, cx, cy; } Box;
static Box g_box[MAXBOX];
static int g_nbox;

static void collect_boxes(void *tf, int depth) {
    if (!tf || depth < 0 || g_nbox >= MAXBOX) return;
    if (m_activeInHierarchy) {
        void *go = inv(m_get_gameObject, tf, NULL);
        void *r  = go ? inv(m_activeInHierarchy, go, NULL) : NULL;
        if (r && !*(uint8_t *)il2cpp_object_unbox(r)) return;
    }
    char nm[64]; tf_name(tf, nm, sizeof nm);
    /* 'btn' is the transparent hit area, not artwork - it is often wider than
       the plate and would pass for one. */
    if (!strcmp(nm, "btn")) return;
    if ((k_Image && get_comp(tf, k_Image)) || (k_TMP && get_comp(tf, k_TMP))) {
        void *rt = get_comp(tf, k_RectTransform);
        void *arr = (rt && k_Vector3 && il2cpp_array_new) ? il2cpp_array_new(k_Vector3, 4) : NULL;
        if (arr) {
            void *a[1] = { arr };
            inv(m_GetWorldCorners, rt, a);
            float *f = (float *)((char *)arr + 32);
            float w = f[6] - f[0], h = f[7] - f[1];
            if (w < 0) w = -w;
            if (h < 0) h = -h;
            if (w > 0.01f && h > 0.01f && g_nbox < MAXBOX) {
                g_box[g_nbox].w = w; g_box[g_nbox].h = h;
                g_box[g_nbox].cx = (f[0] + f[6]) * 0.5f;
                g_box[g_nbox].cy = (f[1] + f[7]) * 0.5f;
                g_nbox++;
            }
        }
    }
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) collect_boxes(tf_child(tf, i), depth - 1);
}

/* The plate a skin draws, as opposed to the glow around it.

   Fitting the panels to their whole drawn box shrank every design by about a
   third, because most skins ring the plate with a wide soft halo that is meant
   to overlap (GX draws 770 units tall out of a 340-unit rect).  ARC-V is the
   one skin where the overflow is real artwork.  What separates them: a plate is
   stacked from several layers of near-identical size - frame, fill, gradient -
   while an effect layer is alone at its size.  So take the biggest box that
   some other visible graphic matches within a fifth. */
static int plate_box(void *tf, float *w, float *h, float *cx, float *cy) {
    g_nbox = 0;
    collect_boxes(tf, 7);
    if (!g_nbox) return 0;
    int best = -1;
    float bestArea = 0.0f;
    for (int i = 0; i < g_nbox; i++) {
        float area = g_box[i].w * g_box[i].h;
        if (area <= bestArea) continue;
        for (int j = 0; j < g_nbox; j++) {
            if (j == i) continue;
            float dw = g_box[i].w - g_box[j].w, dh = g_box[i].h - g_box[j].h;
            if (dw < 0) dw = -dw;
            if (dh < 0) dh = -dh;
            if (dw <= g_box[i].w * 0.2f && dh <= g_box[i].h * 0.2f) {
                best = i; bestArea = area; break;
            }
        }
    }
    if (best < 0)
        for (int i = 0; i < g_nbox; i++) {
            float a = g_box[i].w * g_box[i].h;
            if (a > bestArea) { bestArea = a; best = i; }
        }
    if (best < 0) return 0;
    if (w)  *w  = g_box[best].w;
    if (h)  *h  = g_box[best].h;
    if (cx) *cx = g_box[best].cx;
    if (cy) *cy = g_box[best].cy;
    return 1;
}
static int plate_size(void *tf, float *w, float *h) { return plate_box(tf, w, h, NULL, NULL); }

/* Where the eye puts the middle of this panel.

   A skin draws its plate as a stack of images sharing one centre - Standard
   layers five, ARC-V two - while the decoration around it (a glow, a chain, a
   caption) sits somewhere else.  So: group the graphics by centre and take the
   most populous group, biggest group by area breaking a tie.  The centre of
   that group's largest member is the middle of the panel.

   Three earlier rules all failed here.  Matching boxes by *size* picked ZEXAL's
   caption, which is drawn twice.  The union of everything drawn picked up
   Standard's glow, which reaches further right than the plate and hung the
   window 53px left.  Taking the largest graphic that anything at all is
   concentric with picked that same glow, because Standard's caption happens to
   sit within a rounding error of its centre - one accidental neighbour, against
   the plate's five deliberate ones. */
static int plate_axis(void *tf, float *cx) {
    g_nbox = 0;
    collect_boxes(tf, 7);
    if (!g_nbox) return 0;
    float widest = 0.0f;
    for (int i = 0; i < g_nbox; i++)
        if (g_box[i].w > widest) widest = g_box[i].w;
    const float tol = widest * 0.02f;
    /* A group only counts if it amounts to something: VRAINS draws its plate
       once and its caption twice, and a pair of caption layers must not outvote
       the plate. */
    float maxA = 0.0f;
    for (int i = 0; i < g_nbox; i++) {
        float a = g_box[i].w * g_box[i].h;
        if (a > maxA) maxA = a;
    }
    const float floorA = maxA * 0.25f;
    int best = 0, bestN = 0;
    float bestA = 0.0f, bestW = 0.0f;
    for (int i = 0; i < g_nbox; i++) {
        int n = 0;
        float sum = 0.0f, wid = 0.0f;
        int lead = i;
        for (int j = 0; j < g_nbox; j++) {
            float d = g_box[i].cx - g_box[j].cx;
            if (d < 0) d = -d;
            if (d > tol) continue;
            n++;
            sum += g_box[j].w * g_box[j].h;
            if (g_box[j].w * g_box[j].h > wid) { wid = g_box[j].w * g_box[j].h; lead = j; }
        }
        if (sum < floorA) continue;
        if (n > bestN || (n == bestN && sum > bestA)) {
            bestN = n; bestA = sum; bestW = wid; best = lead;
        }
    }
    (void)bestW;
    if (g_settle == 1)
        nlog("plate: %d boxes, group of %d, plate %.2fx%.2f@%.2f",
             g_nbox, bestN, g_box[best].w, g_box[best].h, g_box[best].cx);
    if (cx) *cx = g_box[best].cx;
    return 1;
}

static int drawn_box(void *tf, float *cx, float *cy, float *w, float *h) {
    float b[4] = { 0, 0, 0, 0 };
    int found = 0;
    g_duNodes = g_duGfx = g_duHidden = 0;
    drawn_union(tf, 7, b, &found);
    if (g_dbgDrawn) {
        char nm[64]; tf_name(tf, nm, sizeof nm);
        nlog("dbg drawn_box(%s): found=%d nodes=%d gfx=%d hidden=%d | corners=%p array_new=%p V3=%p RT=%p",
             nm, found, g_duNodes, g_duGfx, g_duHidden,
             m_GetWorldCorners, (void *)il2cpp_array_new, k_Vector3, get_comp(tf, k_RectTransform));
    }
    if (!found) return 0;
    if (cx) *cx = (b[0] + b[2]) * 0.5f;
    if (cy) *cy = (b[1] + b[3]) * 0.5f;
    if (w)  *w  = b[2] - b[0];
    if (h)  *h  = b[3] - b[1];
    return 1;
}

static void *tf_parent(void *tf) {
    return tf ? inv(meth(k_Transform, "get_parent", 0), tf, NULL) : NULL;
}
static int sibling_index(void *tf) {
    void *p = tf_parent(tf);
    if (!p) return -1;
    int n = tf_children(p);
    for (int i = 0; i < n; i++) if (tf_child(p, i) == tf) return i;
    return -1;
}
static void set_sibling(void *tf, int idx) {
    void *m = meth_sig(k_Transform, "SetSiblingIndex", 1, "System.Int32");
    if (!tf || !m || idx < 0) return;
    void *a[1] = { &idx };
    inv(m, tf, a);
}
static void reparent(void *tf, void *parent) {
    if (!tf) return;
    uint8_t keep = 0;
    void *a[2] = { parent, &keep };
    inv(m_SetParent, tf, a);
}
/* Destroy is deferred to the end of the frame, which is too late when the game
   is about to walk this very hierarchy - so lift it out of the tree first. */
static void discard(void *tf) {
    if (!tf) return;
    reparent(tf, NULL);
    void *go = inv(m_get_gameObject, tf, NULL);
    if (go && m_Destroy) { void *a[1] = { go }; inv(m_Destroy, NULL, a); }
}

/* GetComponents(Type) was stripped from this build, so enumerate the class list
   and ask for each Component subclass one at a time. */
static void probe_components(void *tf, const char *label) {
    if (!tf) return;
    void *imgs[2] = { g_img_cs, g_img_core };
    char line[900]; line[0] = 0;
    for (int im = 0; im < 2; im++) {
        if (!imgs[im] || !il2cpp_image_get_class_count) continue;
        size_t n = il2cpp_image_get_class_count(imgs[im]);
        for (size_t i = 0; i < n; i++) {
            void *k = il2cpp_image_get_class(imgs[im], i);
            if (!k) continue;
            const char *kn = il2cpp_class_get_name(k);
            if (!kn || strchr(kn, '`') || strchr(kn, '<')) continue;
            int isComp = 0;
            for (void *pk = k; pk; pk = il2cpp_class_get_parent(pk))
                if (pk == k_Component) { isComp = 1; break; }
            if (!isComp) continue;
            if (!get_comp(tf, k)) continue;
            if (strlen(line) + strlen(kn) + 2 >= sizeof line) break;
            strcat(line, kn);
            strcat(line, " ");
        }
    }
    nlog("components of %s: %s", label, line[0] ? line : "(none)");
}


/* name / rect / sprite of everything inside a button, so Reset and Undo can be
   made to match Log and Tools */
static void dump_button(void *tf, const char *label) {
    if (!tf) return;
    nlog("--- button %s ---", label);
    dump_rect(tf, label);
    {
        void *img = k_Image ? get_comp(tf, k_Image) : NULL;
        void *sp  = (img && m_get_sprite) ? inv(m_get_sprite, img, NULL) : NULL;
        if (sp) { void *snm = inv(m_get_name, sp, NULL); char sn[96];
                  cs_str(snm, sn, sizeof sn); nlog("      root sprite: %s", sn); }
        else nlog("      root sprite: %s", img ? "(none)" : "(no Image)");
    }
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) {
        void *c = tf_child(tf, i);
        char nm[64]; tf_name(c, nm, sizeof nm);
        char line[160];
        snprintf(line, sizeof line, "%s.%s", label, nm);
        dump_rect(c, line);
        void *img = k_Image ? get_comp(c, k_Image) : NULL;
        if (img && m_get_sprite) {
            void *sp = inv(m_get_sprite, img, NULL);
            if (sp) {
                void *snm = inv(m_get_name, sp, NULL);
                char sn[96]; cs_str(snm, sn, sizeof sn);
                nlog("      sprite: %s", sn);
            }
        }
    }
}


/* Reset and Undo are the same prefab with Undo mirrored, which is why their
   triangles point opposite ways and Reset's caption sits on top.  Each skin
   names and lays out its parts differently, so rather than guess, copy Undo's
   geometry onto Reset part by part - same scale, same anchors, same caption
   strip - and leave the sprites alone so Reset keeps its own icon and wording. */
static void copy_rect_props(void *src, void *dst) {
    void *a2 = get_comp(src, k_RectTransform), *b2 = get_comp(dst, k_RectTransform);
    if (!a2 || !b2) return;
    struct { void *get, *set; } pair[5] = {
        { m_rt_anchorMin,   m_rt_set_anchorMin   },
        { m_rt_anchorMax,   m_rt_set_anchorMax   },
        { m_rt_pivot,       m_rt_set_pivot       },
        { m_rt_sizeDelta,   m_rt_set_sizeDelta   },
        { m_rt_anchoredPos, m_rt_set_anchoredPos },
    };
    for (int i = 0; i < 5; i++) {
        if (!pair[i].get || !pair[i].set) continue;
        float x, y;
        v2(inv(pair[i].get, a2, NULL), &x, &y);
        V2 v = { x, y };
        void *arg[1] = { &v };
        inv(pair[i].set, b2, arg);
    }
    float sx, sy;
    v2(inv(m_localScale, a2, NULL), &sx, &sy);
    V3 sc = { sx, sy, 1.0f };
    void *arg[1] = { &sc };
    inv(m_set_localScale, b2, arg);
}

static void match_reset_to_undo(void *reset, void *undo) {
    if (!reset || !undo) return;
    /* Only the mirroring carries over from the root - copying its position too
       would park Reset on top of Undo. */
    void *ur = get_comp(undo, k_RectTransform), *rr = get_comp(reset, k_RectTransform);
    if (ur && rr) {
        float ux, uy, rx, ry;
        v2(inv(m_localScale, ur, NULL), &ux, &uy);
        v2(inv(m_localScale, rr, NULL), &rx, &ry);
        if (rx < 0.0f) rx = -rx;
        V3 sc = { rx, (uy < 0.0f) ? -rx : rx, 1.0f };
        void *a[1] = { &sc };
        inv(m_set_localScale, rr, a);
    }
    int n = tf_children(undo);
    for (int i = 0; i < n; i++) {
        void *uc = tf_child(undo, i);
        char nm[64]; tf_name(uc, nm, sizeof nm);
        void *rc = find_child(reset, nm);
        if (rc) copy_rect_props(uc, rc);
    }
}

/* Skins draw the four buttons at wildly different sizes, and the rects do not
   reflect that - Duel Monsters gives Reset a small hexagon inside the same
   160x160 rect that Log fills with a big triangle.  Measure what is actually on
   screen (the largest graphic inside each button) and scale the buttons until
   those match.  Runs every frame, so it converges and then holds. */
static float drawn_height(void *b) {
    int n = tf_children(b);
    void *best = NULL;
    float bestArea = 0.0f;
    for (int i = 0; i < n; i++) {
        void *c = tf_child(b, i);
        if (!k_Image || !get_comp(c, k_Image)) continue;
        float cw, ch;
        rect_size(c, &cw, &ch);
        if (cw * ch > bestArea) { bestArea = cw * ch; best = c; }
    }
    if (!best) best = b;
    void *rt = get_comp(best, k_RectTransform);
    if (!rt || !m_GetWorldCorners || !il2cpp_array_new || !k_Vector3) return 0.0f;
    void *arr = il2cpp_array_new(k_Vector3, 4);
    if (!arr) return 0.0f;
    void *a[1] = { arr };
    inv(m_GetWorldCorners, rt, a);
    float *f = (float *)((char *)arr + 32);
    float hgt = f[4] - f[1];                  /* corner1.y (top-left) - corner0.y */
    return hgt < 0 ? -hgt : hgt;
}

static void equalise_buttons(void) {
    float dh[NBTN], target = 0.0f;
    for (int i = 0; i < NBTN; i++) {
        if (!g_btn[i]) return;
        dh[i] = drawn_height(g_btn[i]);
        if (dh[i] < 0.0001f) return;
        if (dh[i] > target) target = dh[i];
    }
    for (int i = 0; i < NBTN; i++) {
        float k = target / dh[i];
        if (k > 0.995f && k < 1.005f) continue;      /* already matched */
        if (k > 1.5f) k = 1.5f;                      /* ease into it */
        void *rt = get_comp(g_btn[i], k_RectTransform);
        if (!rt) continue;
        float sx, sy;
        v2(inv(m_localScale, rt, NULL), &sx, &sy);
        float mag = (sx < 0.0f ? -sx : sx) * k;
        V3 sc = { mag, (sy < 0.0f) ? -mag : mag, 1.0f };
        void *a[1] = { &sc };
        inv(m_set_localScale, rt, a);
    }
}





/* Reset and Undo are the same prefab, one of them mirrored, which is why their
   triangles point opposite ways and their captions sit at different heights.
   Rather than live with that, give all four buttons identical geometry: upright
   transform at the same scale, only the triangle frame flipped so both point the
   same way, and the caption pinned to the same strip at the bottom of the rect. */
static void normalise_button(void *b, int flipFrame) {
    if (!b) return;
    void *rt = get_comp(b, k_RectTransform);
    if (!rt) return;
    V3 one = { 1.0f, 1.0f, 1.0f };
    void *a1[1] = { &one };
    inv(m_set_localScale, rt, a1);

    const char *kid[5] = { "ImageBtn", "Image_eff", "ImageIcon", "text_img", "Text" };
    for (int i = 0; i < 5; i++) {
        void *c = find_child(b, kid[i]);
        void *crt = c ? get_comp(c, k_RectTransform) : NULL;
        if (!crt) continue;
        float sx, sy;
        v2(inv(m_localScale, crt, NULL), &sx, &sy);
        if (sx < 0.0f) sx = -sx;
        if (sy < 0.0f) sy = -sy;
        float wantY = (flipFrame && i < 2) ? -sy : sy;    /* the frame, nothing else */
        V3 cs = { sx, wantY, 1.0f };
        void *a2[1] = { &cs };
        inv(m_set_localScale, crt, a2);
    }

    /* caption strip: same anchor, same offset, on all four */
    void *ti = find_child(b, "text_img");
    void *tir = ti ? get_comp(ti, k_RectTransform) : NULL;
    if (tir) {
        V2 an = { 0.5f, 0.0f }, po = { 0.0f, 10.0f };
        void *x[1] = { &an }, *y[1] = { &po };
        inv(m_rt_set_anchorMin, tir, x);
        inv(m_rt_set_anchorMax, tir, x);
        inv(m_rt_set_anchoredPos, tir, y);
    }
    void *tx = find_child(b, "Text");
    void *txr = tx ? get_comp(tx, k_RectTransform) : NULL;
    if (txr) {
        V2 an = { 0.5f, 0.5f }, po = { 0.0f, -68.0f };
        void *x[1] = { &an }, *y[1] = { &po };
        inv(m_rt_set_anchorMin, txr, x);
        inv(m_rt_set_anchorMax, txr, x);
        inv(m_rt_set_anchoredPos, txr, y);
    }
}



/* put a HorizontalLayoutGroup on the row and tell it to share the width out
   evenly.  Its settings live in serialized fields, so write those rather than
   trust setters the build may have stripped. */
static int add_layout_group(void *rowRt) {
    if (!k_HLG) {
        void *asm_ = il2cpp_domain_assembly_open(g_domain, "UnityEngine.UI");
        void *img  = asm_ ? il2cpp_assembly_get_image(asm_) : NULL;
        k_HLG = img ? il2cpp_class_from_name(img, "UnityEngine.UI", "HorizontalLayoutGroup") : NULL;
        nlog("buttons: HorizontalLayoutGroup class = %p", k_HLG);
    }
    if (!k_HLG) return 0;
    void *go = inv(m_get_gameObject, rowRt, NULL);
    if (!go) return 0;
    void *ta[1] = { il2cpp_type_get_object(il2cpp_class_get_type(k_HLG)) };
    void *g = get_comp(rowRt, k_HLG);
    if (!g) g = inv(m_AddComponent, go, ta);
    if (!g) { nlog("buttons: AddComponent(HorizontalLayoutGroup) failed"); return 0; }
    int   *al = (int *)fld(g, "m_ChildAlignment");
    float *sp = (float *)fld(g, "m_Spacing");
    uint8_t *b;
    if (al) *al = 4;            /* TextAnchor.MiddleCenter */
    if (sp) *sp = 0.0f;
    if ((b = (uint8_t *)fld(g, "m_ChildForceExpandWidth")))  *b = 1;   /* even quarters */
    if ((b = (uint8_t *)fld(g, "m_ChildForceExpandHeight"))) *b = 0;
    if ((b = (uint8_t *)fld(g, "m_ChildControlWidth")))      *b = 0;   /* don't resize them */
    if ((b = (uint8_t *)fld(g, "m_ChildControlHeight")))     *b = 0;
    if ((b = (uint8_t *)fld(g, "m_ChildScaleWidth")))        *b = 0;
    if ((b = (uint8_t *)fld(g, "m_ChildScaleHeight")))       *b = 0;
    nlog("buttons: layout group configured (align=%p spacing=%p)", (void *)al, (void *)sp);
    return 1;
}


/* Reset carries its caption above the icon and Undo below it, so the two rect
   centres are at different heights even when the rects line up.  Align on the
   button's own graphic instead and the icons sit on one line. */
static void *icon_of(void *btnTf) {
    void *bc = k_Button ? get_comp(btnTf, k_Button) : NULL;
    void *g  = bc ? fld_obj(bc, "m_TargetGraphic") : NULL;
    void *tf = g ? inv(m_get_transform, g, NULL) : NULL;
    return tf ? tf : btnTf;
}

/* re-attach to a row we built on an earlier visit to the duel screen */
static int rebind_row(void *row) {
    g_row = row;
    for (int i = 0; i < NBTN; i++) {
        char cn[32], hn[32];
        snprintf(cn, sizeof cn, "ModCell%s", g_btnName[i]);
        snprintf(hn, sizeof hn, "ModHold%s", g_btnName[i]);
        g_cell[i] = find_child(row, cn);
        g_hold[i] = g_cell[i] ? find_child(g_cell[i], hn) : NULL;
        g_btn[i]  = (g_hold[i] && tf_children(g_hold[i]) > 0) ? tf_child(g_hold[i], 0) : NULL;
        if (!g_btn[i]) { nlog("buttons: rebind lost %s", g_btnName[i]); return 0; }
        g_ref[i] = g_btn[i];
    }
    return 1;
}

/* Free up the bottom edge: drop the two buttons we never use, then hand the
   remaining four to one bottom-anchored row that lays them out itself. */
static void arrange_buttons(void *duel) {
    void *existing = find_child(duel, "ModButtonRow");
    if (existing && rebind_row(existing)) {
        g_settle = 240;
        nlog("buttons: row already built, re-settling");
        return;
    }

    {   /* one-shot: what else lives on this screen (close button, help button) */
        static int once;
        if (!once) {
            once = 1;
            void *par = inv(meth(k_Transform, "get_parent", 0), duel, NULL);
            if (par) { dump_tree(par, 0, 3, "duelParent"); }
            int n = tf_children(duel);
            for (int i = 0; i < n; i++) {
                void *c = tf_child(duel, i);
                char s[64]; tf_name(c, s, sizeof s);
                dump_rect(c, s);
            }
            /* GetComponents(Type) is stripped, so ask for every Component
               subclass by name instead - what is bobbing Reset and Undo? */
            void *r0 = find_child(duel, "Reset"), *u0 = find_child(duel, "Undo");
            probe_components(r0, "Reset");
            probe_components(u0, "Undo");
            dump_button(r0, "Reset");
            dump_button(u0, "Undo");
            void *mn0 = find_child(duel, "Menu");
            dump_button(find_child(mn0, "ShowHistory"), "Log");
            dump_button(find_child(mn0, "PlayUtility"), "Tools");
        }
    }
    void *reset = find_child(duel, "Reset");
    void *undo  = find_child(duel, "Undo");
    void *menu  = find_child(duel, "Menu");
    if (!reset || !undo || !menu) { nlog("buttons: reset=%p undo=%p menu=%p", reset, undo, menu); return; }

    void *log   = find_child(menu, "ShowHistory");
    void *tools = find_child(menu, "PlayUtility");
    set_active(find_child(menu, "SetBGM"), 0);
    set_active(find_child(menu, "SearchCardByCamera"), 0);

    void *btn[NBTN] = { log, reset, undo, tools };
    for (int i = 0; i < NBTN; i++)
        if (!btn[i]) { nlog("buttons: missing %s", g_btnName[i]); return; }

    /* remember where each one belongs so the screen can be handed back intact */
    for (int i = 0; i < NBTN; i++) {
        g_btnHome[i]    = tf_parent(btn[i]);
        g_btnHomeIdx[i] = sibling_index(btn[i]);
    }

    set_active(find_child(duel, "Info"), 0);      /* tutorial / help, unused here */

    /* The X is an earlier sibling than LifeArea, so Duelist 1's panel - whose
       touch area covers its whole rect, transparent corners included - was
       swallowing taps meant for it.  Move it to just after LifeArea: above the
       panels, still below every dialog that comes later. */
    {
        void *quit = find_child(duel, "Quit");
        void *la   = find_child(duel, "LifeArea");
        int qi = sibling_index(quit), li = sibling_index(la);
        if (qi >= 0 && li >= 0) {
            g_quitHomeIdx = qi;
            set_sibling(quit, qi < li ? li : li + 1);
            nlog("buttons: Quit moved from %d to just after LifeArea (%d)", qi, li);
        }
    }

    float dw = 0, dh = 0;
    rect_size(duel, &dw, &dh);
    if (dw < 2 || dh < 2) { nlog("buttons: could not read the duel rect"); return; }

    float bw[NBTN], bh[NBTN], rowH = 0;
    for (int i = 0; i < NBTN; i++) {
        rect_size(btn[i], &bw[i], &bh[i]);
        if (bw[i] < 2) bw[i] = dw * 0.08f;
        if (bh[i] < 2) bh[i] = dh * 0.14f;
        if (bh[i] > rowH) rowH = bh[i];          /* the tallest one sets the depth */
    }

    g_row = new_rect(duel, "ModButtonRow");
    if (!g_row) return;
    /* Stretched along the bottom edge and inset from both sides.  The layout
       group shares the remaining width out evenly, so widening the margin is
       the single knob that tightens the gaps between the four buttons. */
    const float half = (g_halfUnits > 1.0f) ? g_halfUnits : dw * 0.5f;
    const float rowW = half * 0.78f;          /* clustered centrally, clear of the outer panels */
    rect_set(g_row, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f, rowW - dw, rowH,
             g_shiftUnits, rowH * 0.12f);
    int laid = add_layout_group(g_row);
    if (!laid) nlog("buttons: no layout group available, anchoring cells by fraction");

    for (int i = 0; i < NBTN; i++) {
        char cn[32], hn[32];
        snprintf(cn, sizeof cn, "ModCell%s", g_btnName[i]);
        snprintf(hn, sizeof hn, "ModHold%s", g_btnName[i]);
        g_cell[i] = new_rect(g_row, cn);
        if (!g_cell[i]) return;
        if (laid) {
            rect_set(g_cell[i], 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, bw[i], bh[i], 0.0f, 0.0f);
        } else {
            float f = (i + 0.5f) / (float)NBTN;   /* same even split, done by anchor */
            rect_set(g_cell[i], f, 0.5f, f, 0.5f, 0.5f, 0.5f, bw[i], bh[i], 0.0f, 0.0f);
        }
        g_hold[i] = new_rect(g_cell[i], hn);
        if (!g_hold[i]) return;
        rect_set(g_hold[i], 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f);
        uint8_t keep = 0;
        void *sp[2] = { g_hold[i], &keep };
        inv(m_SetParent, btn[i], sp);
        g_btn[i] = btn[i];
        g_ref[i] = btn[i];
    }
    /* Our row was appended last, so it sat on top of the Log and Tools popups.
       Slot it in right after Menu - above the panels, below every dialog. */
    {
        int mi = sibling_index(menu);
        if (mi >= 0) set_sibling(g_row, mi + 1);
    }
    g_duelNode = duel;
    /* lift the X clear of the top row - a fraction of the screen, not pixels */
    g_quitY = -dh * 0.062f;
    g_settle = 240;
    nlog("buttons: one %.0fpx row across a %.0fx%.0f duel screen, layout group=%d", rowH, dw, dh, laid);
}

/* The game rewrites each button's own transform every frame, so rather than
   fight it we shift the parent we own until the button lands on its cell.
   Two or three frames and the error is gone; after that this is a no-op. */
static void settle_buttons(void) {
    if (!g_row) return;
    /* game code re-drives these every frame, so re-assert them */
    match_reset_to_undo(g_btn[1], g_btn[2]);
    equalise_buttons();

    /* The Timer and the close button belong to the game, but they inherit the
       same off-centre canvas, so give them the same correction.  The X also
       moves up out of the top row's way. */
    if (g_duelNode) {
        /* Forcing anchoredPosition.x to the recentring shift only centres the
           Timer on the skins whose timer rect happens to be centred on the bar
           it draws - Simple and GX draw theirs off to one side of the rect, so
           they came out visibly right of the buttons.  Close the loop instead:
           measure the bar the skin actually draws, aim it at the middle of the
           button row (which is where the glass centre is), and correct.  Damped,
           so an imperfect units-per-world estimate still converges. */
        void *tm = find_child(g_duelNode, "Timer");
        void *rt = tm ? get_comp(tm, k_RectTransform) : NULL;
        if (rt) {
            /* Two ways of moving it both failed.  Writing the correction into
               the Timer's own anchoredPosition is undone before it renders -
               something on this skin puts it back after StartDuel.Update, so
               the loop, which measures right after its own write, was reading a
               timer that never reached the screen.  Shifting its children moved
               only part of what the timer draws, because the root carries an
               image of its own, so the loop saw half the movement and asked for
               twice the correction.  Move the anchor instead: nothing else
               touches it, anchoredPosition can be rewritten all it likes, and
               the whole timer travels with it. */
            static void *forTm; static float homeMin, homeMax, parW;
            if (tm != forTm) {
                forTm = tm;
                float a, b;
                v2(inv(m_rt_anchorMin, rt, NULL), &a, &b); homeMin = a;
                v2(inv(m_rt_anchorMax, rt, NULL), &a, &b); homeMax = a;
                float ph = 0; parW = 0;
                void *par = tf_parent(tm);
                if (par) rect_size(par, &parW, &ph);
                if (parW < 1.0f) parW = 2003.0f;
                nlog("tmr: anchors %.4f..%.4f across %.0f", homeMin, homeMax, parW);
            }
            {
                float d = g_timerFix / parW;
                float mnx, mny, mxx, mxy;
                v2(inv(m_rt_anchorMin, rt, NULL), &mnx, &mny);
                v2(inv(m_rt_anchorMax, rt, NULL), &mxx, &mxy);
                float wantMin = homeMin + d, wantMax = homeMax + d;
                if (mnx < wantMin - 0.0002f || mnx > wantMin + 0.0002f) {
                    V2 v = { wantMin, mny };
                    void *a[1] = { &v };
                    inv(m_rt_set_anchorMin, rt, a);
                }
                if (mxx < wantMax - 0.0002f || mxx > wantMax + 0.0002f) {
                    V2 v = { wantMax, mxy };
                    void *a[1] = { &v };
                    inv(m_rt_set_anchorMax, rt, a);
                }
            }
            float x = g_timerFix;
            float tcx, tcy, tw, th, rcx = 0, rcy = 0;
            if (g_settle > 0 && drawn_box(tm, &tcx, &tcy, &tw, &th) && glass_centre_world(&rcx)) {
                (void)rcy;
                float rw = 0, rh = 0;
                rect_size(tm, &rw, &rh);
                float wcx, wcy;
                world_centre(tm, &wcx, &wcy);
                float worldW = 0, dummy = 0;
                {   /* world units per rect unit, straight off the same rect */
                    void *arr = il2cpp_array_new(k_Vector3, 4);
                    if (arr) {
                        void *a[1] = { arr };
                        inv(m_GetWorldCorners, rt, a);
                        float *f = (float *)((char *)arr + 32);
                        worldW = f[6] - f[0];
                    }
                    (void)dummy;
                }
                float k = (rw > 1.0f && worldW > 1.0f) ? rw / worldW : 1.0f;
                float e = (rcx - tcx) * k;
                float err = rcx - tcx;
                if (e > 0.5f || e < -0.5f) g_timerFix += e * 0.6f;
            }
        }
        {   /* The button row is a group of its own and has to answer to the
               screen, not to where the game happens to hang its UI: with the
               panels centred on the glass, a row still sitting on the safe-area
               centre reads as the panels being pushed left. */
            void *rrt = g_row ? get_comp(g_row, k_RectTransform) : NULL;
            float gcx = 0;
            if (rrt && g_settle > 0 && glass_centre_world(&gcx)) {
                float x, y;
                v2(inv(m_rt_anchoredPos, rrt, NULL), &x, &y);
                float want = g_rowFix;
                if (x < want - 0.5f || x > want + 0.5f) {
                    V2 v = { want, y };
                    void *a[1] = { &v };
                    inv(m_rt_set_anchoredPos, rrt, a);
                }
                float bcx, bcy, bw, bh;
                if (drawn_box(g_row, &bcx, &bcy, &bw, &bh)) {
                    float rw = 0, rh = 0;
                    rect_size(g_row, &rw, &rh);
                    float worldW = 0;
                    void *arr = il2cpp_array_new(k_Vector3, 4);
                    if (arr) {
                        void *aa[1] = { arr };
                        inv(m_GetWorldCorners, rrt, aa);
                        float *f = (float *)((char *)arr + 32);
                        worldW = f[6] - f[0];
                    }
                    float k = (rw > 1.0f && worldW > 1.0f) ? rw / worldW : 1.0f;
                    /* err is in world units; the tolerance has to be in the rect
                       units the correction is written in, or half a world unit -
                       fifty pixels - counts as centred. */
                    float e = (gcx - bcx) * k;
                    float err = gcx - bcx;
                    if (e > 0.5f || e < -0.5f) g_rowFix += e * 0.6f;
                }
            }
        }
        void *q = find_child(g_duelNode, "Quit");
        void *qr = q ? get_comp(q, k_RectTransform) : NULL;
        if (qr && g_quitY < -0.5f) {
            float x, y;
            v2(inv(m_rt_anchoredPos, qr, NULL), &x, &y);
            if (y < g_quitY - 0.5f || y > g_quitY + 0.5f) {
                V2 v = { x, g_quitY };
                void *a[1] = { &v };
                inv(m_rt_set_anchoredPos, qr, a);
            }
        }
    }
    if (g_settle > 0) g_settle--;
    for (int i = 0; i < NBTN; i++) {
        float cx, cy, bx, by, hx, hy, hz;
        if (!world_centre(g_cell[i], &cx, &cy)) continue;
        if (!world_centre(g_ref[i],  &bx, &by)) continue;
        if (!world_pos(g_hold[i], &hx, &hy, &hz)) continue;
        float ex = cx - bx, ey = cy - by;
        if (ex * ex + ey * ey < 0.0001f) continue;
        set_world(g_hold[i], hx + ex, hy + ey, hz);
    }
}

/* A bare RectTransform we own, created empty under parentTf.  Reused if a
   previous visit to this screen already made one with the same name. */
static void *new_rect(void *parentTf, const char *name) {
    void *ex = find_child(parentTf, name);
    if (ex) return get_comp(ex, k_RectTransform);
    if (!m_GO_ctor || !m_AddComponent || !il2cpp_object_new) {
        nlog("rect %s: missing dep (ctor=%p add=%p new=%p)", name,
             m_GO_ctor, m_AddComponent, (void *)il2cpp_object_new);
        return NULL;
    }
    void *go = il2cpp_object_new(k_GameObject);
    if (!go) { nlog("rect %s: object_new failed", name); return NULL; }
    void *na[1] = { il2cpp_string_new(name) };
    inv(m_GO_ctor, go, na);
    inv(m_set_name, go, na);
    void *ta[1] = { il2cpp_type_get_object(il2cpp_class_get_type(k_RectTransform)) };
    void *rt = inv(m_AddComponent, go, ta);
    if (!rt) { nlog("rect %s: AddComponent failed", name); return NULL; }
    uint8_t keep = 0;
    void *sp[2] = { parentTf, &keep };
    inv(m_SetParent, rt, sp);
    return rt;
}

static void rect_set(void *rt, float amnx, float amny, float amxx, float amxy,
                     float pvx, float pvy, float w, float h, float px, float py) {
    if (!rt) return;
    V2 amn = { amnx, amny }, amx = { amxx, amxy }, pv = { pvx, pvy },
       sd  = { w, h },       ap  = { px, py };
    void *a[1];
    a[0] = &amn; inv(m_rt_set_anchorMin, rt, a);
    a[0] = &amx; inv(m_rt_set_anchorMax, rt, a);
    a[0] = &pv;  inv(m_rt_set_pivot, rt, a);
    a[0] = &sd;  inv(m_rt_set_sizeDelta, rt, a);
    a[0] = &ap;  inv(m_rt_set_anchoredPos, rt, a);
    V3 one = { 1, 1, 1 };
    a[0] = &one; inv(m_set_localScale, rt, a);
}

/* A full-stretch wrapper for one LP panel.  The game animates the panel's own
   transform every frame, so the arrangement has to live on a parent. */
static void *make_wrapper(void *parentTf, const char *name, float dx, float dy, float scale) {
    void *rt = new_rect(parentTf, name);
    if (!rt) return NULL;
    rect_set(rt, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, dx, dy);
    V3 sc = { scale, scale, 1.0f };
    void *as[1] = { &sc };
    inv(m_set_localScale, rt, as);
    return rt;
}


/* Where the middle of the screen is.

   The phone has a display cutout that takes a 126px strip off one edge, and the
   game centres its entire UI on the *safe area* rather than on the glass - the
   stock two-player screen measures dead centre at 1268 of 2412, not 1205.  So
   the middle of the screen, as far as this app and the eye are concerned, is the
   middle of the safe area, and our layout simply inherits it by staying on the
   canvas centre.

   An earlier version of this took the cutout out of the canvas position, on the
   theory that the layout was drifting right.  It was not: the drift was the
   middle window and the Timer being measured by their pivots rather than by
   what they draw (GetWorldCorners was never wired up - il2cpp_array_new had not
   been resolved), and correcting the whole layout for it left everything 63px
   left of every other screen in the game. */
static void measure_screen(void *la, float w) {
    (void)la;
    g_shiftUnits = 0.0f;
    g_halfUnits  = w * 0.5f;
    g_edgeUnits  = 0.0f;
    if (!m_screen_w || !m_screen_sa) return;
    void *swr = inv(m_screen_w, NULL, NULL);
    void *sa  = inv(m_screen_sa, NULL, NULL);
    if (!swr || !sa) return;
    float screenW = (float)(*(int *)il2cpp_object_unbox(swr));
    float *r = (float *)il2cpp_object_unbox(sa);
    if (screenW < 1.0f || r[2] < 1.0f || r[2] > screenW) return;
    /* The layout sits on the safe-area centre, but the glass runs further one
       way than the other from there: on this phone the cutout is all on one
       side, so the far edge is 63px nearer than the near one.  Symmetric
       columns therefore have to be budgeted against the *shorter* half, or the
       outer panel on the cutout-free side hangs off the screen - which is what
       cropped the ARC-V shields. */
    float off = (r[0] + r[2] * 0.5f) - screenW * 0.5f;
    if (off < 0.0f) off = -off;
    g_edgeUnits = off * (w / screenW);
    nlog("screen: %.0fpx wide, safe area %.0f..%.0f - centred on it, %.0f units of edge lost",
         screenW, r[0], r[0] + r[2], g_edgeUnits);
}

/* 2x2 grid: each stock panel is moved into a wrapper we position and scale */
static void build_four_player_layout(void *self) {
    void *la = find_life_area(self);
    if (!la) return;
    if (la == g_wrappedArea) return;                  /* already arranged */
    char nm[64];

    /* The LifeArea rect animates in, so an early read gives a rect that is a
       few per cent short and the panels land in the wrong places.  Wait for it
       to hold still. */
    float w = 0, h = 0;
    {
        void *lart0 = get_comp(la, k_RectTransform);
        if (lart0 && m_rt_rect) {
            void *r = inv(m_rt_rect, lart0, NULL);
            if (r) { float *f = (float *)il2cpp_object_unbox(r); w = f[2]; h = f[3]; }
        }
        if (w < 1 || h < 1) { g_stableN = 0; return; }
        float dw2 = w - g_lastW, dh2 = h - g_lastH;
        if (dw2 < 0) dw2 = -dw2;
        if (dh2 < 0) dh2 = -dh2;
        if (dw2 > 0.5f || dh2 > 0.5f) { g_lastW = w; g_lastH = h; g_stableN = 0; return; }
        if (++g_stableN < 5) return;
    }

    g_dumpGfx = 1;
    const int np = mode_players(g_selMode);       /* 5 and 3 both put one in the middle */
    int n = tf_children(la);
    if (n < np) {
        for (int i = n; i < np; i++) {
            void *src = tf_child(la, i % 2);      /* alternate so each column keeps its mirroring */
            void *go = inv(m_get_gameObject, src, NULL);
            void *a1[1] = { go };
            void *clone = inv(m_Instantiate, NULL, a1);
            if (!clone) continue;
            void *ctf = inv(m_go_get_transform, clone, NULL);
            uint8_t keep = 0;
            void *sp[2] = { la, &keep };
            if (ctf) inv(m_SetParent, ctf, sp);
            char cn[32];
            snprintf(cn, sizeof cn, "Life0%d", i + 1);
            void *na[1] = { il2cpp_string_new(cn) };
            inv(m_set_name, clone, na);
        }
        n = tf_children(la);
    }
    if (n < np) { nlog("4P: only %d panels, wanted %d", n, np); return; }

    nlog("4P: LifeArea is %.0fx%.0f", w, h);

    /* on a second visit the direct children are our own slots, not the panels */
    void *panels[5];
    int wrapped = find_child(la, "ModSlot0") != NULL;
    for (int i = 0; i < np; i++) {
        if (wrapped) {
            char s[16];
            snprintf(s, sizeof s, "ModSlot%d", i);
            void *w = find_child(la, s);
            void *hd = w ? find_child(w, "ModPanelHold") : NULL;
            panels[i] = (hd && tf_children(hd) > 0) ? tf_child(hd, 0)
                      : ((w && tf_children(w) > 0) ? tf_child(w, 0) : NULL);
        } else {
            panels[i] = tf_child(la, i);
        }
    }

    /* Half scale halves the usable area; a quarter-width shift puts the panels hard
       against the screen edges, so pull them in a little.  The bottom row is raised
       further to clear the Log / Tools / Music / Camera buttons along the bottom. */
    const float topMargin = h * 0.012f;      /* clear of the status bar */
    /* Each Calculator Design skin ships a differently proportioned panel - the
       Simple one is far shorter and wider than Standard - so fit the panel into
       its share of the screen on both axes and take whichever is tighter.  The
       two fractions are the Standard design's own proportions, so it comes out
       at exactly the scale it had before. */
    float pw = 0, ph = 0;
    rect_size(panels[0], &pw, &ph);
    /* The rect is the game's idea of the panel; the plate is what the eye sees.
       They agree on most skins, but ARC-V draws a shield half again as tall as
       its rect, which is why it came out overlapping its neighbours.  Fit to
       whichever is bigger - skins that stay inside their rect are untouched. */
    {
        float plw = 0, plh = 0;
        if (plate_size(panels[0], &plw, &plh) && pw > 1.0f && ph > 1.0f) {
            float worldW = 0, worldH = 0;
            void *prt = get_comp(panels[0], k_RectTransform);
            void *arr = (prt && k_Vector3 && il2cpp_array_new) ? il2cpp_array_new(k_Vector3, 4) : NULL;
            if (arr) {
                void *a[1] = { arr };
                inv(m_GetWorldCorners, prt, a);
                float *f = (float *)((char *)arr + 32);
                worldW = f[6] - f[0];
                worldH = f[7] - f[1];
                if (worldW < 0) worldW = -worldW;
                if (worldH < 0) worldH = -worldH;
            }
            if (worldW > 0.001f && worldH > 0.001f) {
                float prw = plw * (pw / worldW), prh = plh * (ph / worldH);
                nlog("4P: rect %.0fx%.0f, plate %.0fx%.0f", pw, ph, prw, prh);
                if (prw > pw) pw = prw;
                if (prh > ph) ph = prh;
            }
        }
        dump_graphics(panels[0], 0, "");
    }

    /* Five panels have to share the width with one in the middle, so they get a
       narrower budget than four do. */
    measure_screen(la, w);
    const float vis = g_halfUnits * 2.0f;             /* canvas units actually on screen */
    /* Budget per panel.  Now that the fit is against the plate rather than the
       rect, the same fractions would leave every skin smaller than before, so
       they go up: the glow is allowed to spill past its share, only the plate
       has to fit.  Width is what keeps the outer columns clear of the middle
       one, height is what keeps the two rows apart. */
    const float maxW = vis * (np == 5 ? 0.30f : 0.32f);
    /* Height budget.  The two skins this binds on are the round ones - 5D's and
       ARC-V - and both were asked to be bigger; the bottom-row clamp below keeps
       them off the edge. */
    const float maxH = h * 0.50f;
    float byW = (pw > 1.0f) ? maxW / pw : 1.0f;
    float byH = (ph > 1.0f) ? maxH / ph : 1.0f;
    const float scale = (byW < byH) ? byW : byH;
    const float sw = pw * scale;
    nlog("4P: panel %.0fx%.0f -> scale %.3f (width %.3f, height %.3f)", pw, ph, scale, byW, byH);

    /* Sit the outer columns hard against the edges without hanging off them -
       the Simple skin fills its rect completely, so any overhang is a visible
       crop rather than the Standard skin's transparent bleed. */
    const float sh = g_shiftUnits;
    /* Keep the outer columns off the edges - on 5D's the Quit cross sits in the
       top-left corner and was landing on the first duelist's name. */
    float cx = g_halfUnits - sw * 0.5f - vis * 0.035f;
    /* The block is centred on the glass now, so both halves are the same and
       there is no short side to budget against. */
    const float lim = w * 0.5f - sw * 0.5f - w * 0.035f;
    if (cx > lim) cx = lim;
    /* Three duelists use the four-screen grid with the bottom row reduced to one
       panel, centred: two across the top, the third below and between them. */
    const float tx[5] = { sh - cx, sh + cx, (np == 3) ? sh : sh - cx, sh + cx, sh };
    /* Space the rows off the panel's own height - a fixed fraction left the
       taller skins (GX) touching. */
    const float sh2 = ph * scale;
    float cy = sh2 * 0.5f + h * 0.030f;
    /* The plate is what gets fitted, but a skin can draw a little past it - the
       ARC-V shield's tail is about a tenth of its height - and the whole grid is
       already pushed down by topMargin, so the bottom row is where that shows.
       Leave the difference as clearance. */
    const float cyMax = h * 0.5f - sh2 * 0.52f - h * 0.010f - topMargin;
    if (cy > cyMax) cy = cyMax;
    /* The third panel goes on the bottom row, not at the centre of the screen:
       parked at the centre it left the block hanging from the top with an empty
       band underneath.  On the bottom row it is symmetric about the centre. */
    const float ty[5] = { cy - topMargin, cy - topMargin,
                          -cy - topMargin, -cy - topMargin,
                          -topMargin };

    g_np = np;
    for (int i = 0; i < np; i++) {
        snprintf(nm, sizeof nm, "ModSlot%d", i);
        if (!panels[i]) continue;
        /* Slot marks the spot; the holder underneath absorbs whatever offset the
           panel prefab carries.  Measuring that beats predicting it - the skins
           anchor and pivot their panels differently. */
        void *wrt = make_wrapper(la, nm, tx[i], ty[i], scale);
        if (!wrt) continue;
        void *hold = new_rect(wrt, "ModPanelHold");
        if (!hold) continue;
        rect_set(hold, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f);
        uint8_t keep = 0;
        void *sp[2] = { hold, &keep };
        inv(m_SetParent, panels[i], sp);
        g_slot[i] = wrt; g_phold[i] = hold; g_panel[i] = panels[i];
    }
    g_psettle = 240;

    /* Deliberately not searching here: the panels have only just been cloned and
       the skin has not filled them in yet.  The per-frame pass below waits for
       the numbers to appear and then decides. */
    g_capLen = -1; g_artLen = -1; g_txtLen = -1; g_difLen = -1;
    g_capName[0] = g_artName[0] = g_txtName[0] = g_difName[0] = 0;
    g_capSaid = 0;
    { static int once; if (!once) { once = 1;
        nlog("--- panel diff (Life01 vs Life02) ---");
        dump_panel_diff(panels[0], panels[1], 5, ""); } }
    g_labelled = 0;
    wire_panels();
    {   /* LifeArea's parent is the Duel node that holds Reset / Undo / Menu */
        void *duel = inv(meth(k_Transform, "get_parent", 0), la, NULL);
        if (duel) arrange_buttons(duel);
    }
    g_dumpGfx = 0;
    g_wrappedArea = la;
    nlog("4P: %d panels wrapped at scale %.3f (LifeArea now has %d children)", np, scale, tf_children(la));
    for (int i = 0; i < tf_children(la) && i < 5; i++) {
        void *c = tf_child(la, i);
        char cn[64]; tf_name(c, cn, sizeof cn);
        dump_rect(c, cn);
    }
}


/* Hand the screen back exactly as the game built it.  StartDuel.SetCalcObject
   looks its pieces up by path, so a ModSlot sitting where Life01 should be
   makes OnEnable throw and the calculator comes up blank.  Everything is
   rebuilt from scratch a few frames later, from Update. */

/* Where the middle of the glass is, in world units.

   The game centres its whole UI on the safe area, which on this phone is 63px
   right of the middle of the screen because the cutout takes a strip off the
   left.  The timer and the button row are small and centred, so that offset
   does not read; the panel block runs to both edges, so it does - the gap on
   the left came out more than twice the gap on the right.  The block is
   therefore centred on the glass instead, and the root canvas - which for a
   screen-space overlay covers the whole display - is what says where that is. */
static int glass_centre_world(float *out) {
    if (!g_duelNode || !m_GetWorldCorners || !k_Vector3 || !il2cpp_array_new) return 0;
    void *mParent = meth(k_Transform, "get_parent", 0);
    void *root = g_duelNode, *up = g_duelNode;
    while (up) {
        void *p = inv(mParent, up, NULL);
        if (!p) break;
        if (get_comp(p, k_RectTransform)) root = p;
        up = p;
    }
    void *rt = get_comp(root, k_RectTransform);
    if (!rt) return 0;
    void *arr = il2cpp_array_new(k_Vector3, 4);
    if (!arr) return 0;
    void *a[1] = { arr };
    inv(m_GetWorldCorners, rt, a);
    float *f = (float *)((char *)arr + 32);
    *out = (f[0] + f[6]) * 0.5f;
    return 1;
}

static float glass_shift(void) {
    if (!g_duelNode || !m_GetWorldCorners || !k_Vector3 || !il2cpp_array_new) return 0.0f;
    void *mParent = meth(k_Transform, "get_parent", 0);
    void *root = g_duelNode, *up = g_duelNode;
    while (up) {
        void *p = inv(mParent, up, NULL);
        if (!p) break;
        if (get_comp(p, k_RectTransform)) root = p;
        up = p;
    }
    void *rt = get_comp(root, k_RectTransform);
    if (!rt) return 0.0f;
    void *arr = il2cpp_array_new(k_Vector3, 4);
    if (!arr) return 0.0f;
    void *a[1] = { arr };
    inv(m_GetWorldCorners, rt, a);
    float *f = (float *)((char *)arr + 32);
    float glass = (f[0] + f[6]) * 0.5f;
    float mid = 0.0f;
    if (g_slot[4]) { float sy; world_centre(g_slot[4], &mid, &sy); }
    else if (g_slot[0] && g_slot[1]) {
        float x0, x1, y;
        world_centre(g_slot[0], &x0, &y);
        world_centre(g_slot[1], &x1, &y);
        mid = (x0 + x1) * 0.5f;
    }
    return glass - mid;
}

static void mod_teardown(void *self) {
    void *la   = find_life_area(self);
    void *duel = tf_parent(la);
    if (!duel) return;

    void *row = find_child(duel, "ModButtonRow");
    if (row) {
        for (int i = 0; i < NBTN; i++) {
            char cn[32], hn[32];
            snprintf(cn, sizeof cn, "ModCell%s", g_btnName[i]);
            snprintf(hn, sizeof hn, "ModHold%s", g_btnName[i]);
            void *cell = find_child(row, cn);
            void *hold = cell ? find_child(cell, hn) : NULL;
            void *b    = (hold && tf_children(hold) > 0) ? tf_child(hold, 0) : NULL;
            if (b && g_btnHome[i]) { reparent(b, g_btnHome[i]); set_sibling(b, g_btnHomeIdx[i]); }
        }
        discard(row);
    }
    void *menu = find_child(duel, "Menu");
    set_active(find_child(duel, "Info"), 1);
    set_active(find_child(menu, "SetBGM"), 1);
    set_active(find_child(menu, "SearchCardByCamera"), 1);
    if (g_quitHomeIdx >= 0) { set_sibling(find_child(duel, "Quit"), g_quitHomeIdx); g_quitHomeIdx = -1; }

    if (la) {
        for (int i = 0; i < 5; i++) {
            char nm[16];
            snprintf(nm, sizeof nm, "ModSlot%d", i);
            void *slot = find_child(la, nm);
            if (!slot) continue;
            void *hold = find_child(slot, "ModPanelHold");
            void *pan  = (hold && tf_children(hold) > 0) ? tf_child(hold, 0)
                       : (tf_children(slot) > 0 ? tf_child(slot, 0) : NULL);
            if (pan) reparent(pan, la);
            discard(slot);
        }
        discard(find_child(la, "Life03"));
        discard(find_child(la, "Life04"));
        discard(find_child(la, "Life05"));
    }

    memset(g_btn, 0, sizeof g_btn);
    memset(g_btnHome, 0, sizeof g_btnHome);
    memset(g_slot, 0, sizeof g_slot);
    memset(g_phold, 0, sizeof g_phold);
    memset(g_panel, 0, sizeof g_panel);
    g_np = 0; g_psettle = 0; g_labelled = 0; g_capLen = -1; g_capIsSprite = 0; g_artLen = -1; g_txtLen = -1; g_difLen = -1;
    g_capName[0] = g_artName[0] = g_txtName[0] = g_difName[0] = 0;
    g_capSaid = 0;
    g_row = NULL; g_settle = 0; g_wrappedArea = NULL; g_stableN = 0; g_timerFix = 0.0f; g_rowFix = 0.0f;
    nlog("teardown: screen handed back in stock shape");
}



/* Same trick as the button row: the panels are moved by game code every frame,
   so nudge the parent we own until each one sits on its slot. */
static void settle_panels(void) {
    if (!g_np) return;
    if (g_psettle > 0) g_psettle--;

    /* The plate a skin draws is not centred in its panel rect, and for the lone
       middle window that shows.  Earlier this inferred the offset from the
       mirrored left/right pair and applied a calibrated fraction of it, which
       silently did nothing on the skins whose largest graphic happens to be
       symmetric (5D's ring, ZEXAL's plate).  Measure the middle panel's own
       drawn box instead and put *that* on the slot - no coefficient to tune,
       and it is the same quantity the eye is judging. */

    /* the localiser fills the caption in a frame or two after the screen opens,
       so keep trying until there is text to diff, then rewrite the clones once */
    /* A rename lands long after the panels have settled, and the labelling only
       runs inside the settle window - so reopen it. */
    if (g_nameDirty) { g_labelled = 0; g_psettle = 30; g_nameDirty = 0; }
    /* Keep trying for as long as the screen is up, not just while the panels are
       settling: the captions arrive from the localiser at their own pace, and on
       a skin where they were late the names were never written at all - Duel
       Monsters came up with its prefab's own DUELIST 01 on every panel. */
    /* Re-assert while the screen settles: the game re-draws its own two panels
       from its own state, and a caption written once was put back. */
    if ((!g_labelled || g_psettle > 0) && g_np) {
        if (g_capLen < 0) {
            if (!panel_ready(g_panel[0]) || !panel_ready(g_panel[1])) return;
            g_capLen = -1; g_artLen = -1; g_txtLen = -1; g_difLen = -1;
    g_capName[0] = g_artName[0] = g_txtName[0] = g_difName[0] = 0;
    g_capSaid = 0;
            diff_caption(g_panel[0], g_panel[1], 6, 0);
            if (caption_is_the_score(g_panel[0])) {
                nlog("cap: rejected - the search landed on LifePoints");
                g_capLen = -1; g_artLen = -1; g_txtLen = -1; g_difLen = -1;
    g_capName[0] = g_artName[0] = g_txtName[0] = g_difName[0] = 0;
    g_capSaid = 0;
                return;
            }
        }
        {
            static int said;
            if (g_capLen != said) {
                said = g_capLen;
                char nm[64] = "?";
                void *n0 = cap_node(g_panel[0]);
                if (n0) tf_name(n0, nm, sizeof nm);
                nlog("cap: len=%d sprite=%d artLen=%d node '%s'",
                     g_capLen, g_capIsSprite, g_artLen, nm);
            }
        }
        if (g_capLen >= 0) {
            char src[64] = "";
            void *srcNode = cap_node(g_panel[0]);
            void *srcTmp  = srcNode && k_TMP ? get_comp(srcNode, k_TMP) : NULL;
            if (srcTmp && m_get_text) cs_str(inv(m_get_text, srcTmp, NULL), src, sizeof src);
            for (int i = 0; i < g_np; i++) {
                char cap[64];
                if (g_pname[i][0]) snprintf(cap, sizeof cap, "%s", g_pname[i]);
                else if (i >= 2)   caption_from(src, i + 1, cap, sizeof cap);
                else continue;                 /* the game captions its own two */
                label_panel(g_panel[i], cap);
                if (!g_labelled) nlog("cap: panel %d <- '%s'", i + 1, cap);
            }
            g_labelled = 1;
            g_nameDirty = 0;
        }
    }

    if (g_psettle == 60 && g_np) {          /* settled: report what landed where */
        float cx, cy, w, h, rcx, rcy;
        if (g_row && world_centre(g_row, &rcx, &rcy))
            nlog("geom: row centre x=%.1f (shift=%.1f)", rcx, g_shiftUnits);
        void *tm = g_duelNode ? find_child(g_duelNode, "Timer") : NULL;
        if (tm && drawn_box(tm, &cx, &cy, &w, &h)) {
            float trx, try_;
            world_centre(tm, &trx, &try_);
            nlog("geom: timer rect x=%.2f drawn x=%.2f w=%.2f (fix=%.1f)", trx, cx, w, g_timerFix);
        }
        for (int i = 0; i < g_np; i++) {
            float ax, ay;
            if (!world_centre(g_slot[i], &ax, &ay)) continue;
            float rx, ry;
            world_centre(g_panel[i], &rx, &ry);
            if (drawn_box(g_panel[i], &cx, &cy, &w, &h))
                nlog("geom: panel%d slot x=%.2f rect x=%.2f drawn x=%.2f (%.2fx%.2f)",
                     i, ax, rx, cx, w, h);
        }
    }

    const float gshift = glass_shift();
    for (int i = 0; i < g_np; i++) {
        float sx, sy, px, py, hx, hy, hz;
        if (!world_centre(g_slot[i],  &sx, &sy)) continue;
        if (!world_centre(g_panel[i], &px, &py)) continue;
        if (!world_pos(g_phold[i], &hx, &hy, &hz)) continue;
        sx += gshift;
        if (i == 4) {
            /* The middle window has no mirror partner, so whatever offset the
               plate has inside its rect is on show.  Align the plate, not the
               rect and not the union of everything drawn: the union counts the
               glow, which on the Standard skin reaches 1.3 units further right
               than the plate and hung the window 53px to the left.  Fall back to
               the union on a skin where no plate can be identified. */
            float dx, dy, dw, dh;
            if (plate_axis(g_panel[i], &dx)) px = dx;
            else if (drawn_box(g_panel[i], &dx, &dy, &dw, &dh)) px = dx;
        }
        float ex = sx - px, ey = sy - py;
        if (ex * ex + ey * ey < 0.0001f) continue;
        set_world(g_phold[i], hx + ex, hy + ey, hz);
    }
}

/* Five independent life counters.

   Duel keeps its players in a Dictionary<int,Player> and everything downstream
   is already player-indexed: GetLife(i), AddLife(i,v), GetHistory(i),
   LifeHistory.PlayerIndex, LifeLog.TargetPlayer.  Only the *creation* is fixed
   at two - CreatePlayers builds player 0 and 1 and nothing else - and the UI
   knows about two panels.  So: let the game build its two, then add the rest to
   the same dictionary, and point the cloned panels' buttons at indices 2..4. */
static void *g_duelModel;
static void *k_DuelPlayer;
static int   g_extraPlayers;
static void (*orig_CreatePlayers)(void *, int, int, int, void *);

static void add_players(void *duel, int upto) {
    if (!duel) return;
    void *dict = *(void **)((char *)duel + 16);
    if (!dict) { nlog("players: no dictionary on the duel"); return; }
    void *dc = il2cpp_object_get_class(dict);
    void *mAdd = dc ? il2cpp_class_get_method_from_name(dc, "Add", 2) : NULL;
    void *mGet = dc ? il2cpp_class_get_method_from_name(dc, "get_Item", 1) : NULL;
    void *mCount = dc ? il2cpp_class_get_method_from_name(dc, "get_Count", 0) : NULL;
    if (!mAdd || !mGet) { nlog("players: Dictionary Add/get_Item missing"); return; }
    int zero = 0;
    void *ga[1] = { &zero };
    void *p0 = inv(mGet, dict, ga);
    if (!p0) { nlog("players: player 0 missing"); return; }
    void *pc = il2cpp_object_get_class(p0);
    k_DuelPlayer = pc;
    void *fLife = pc ? il2cpp_class_get_field_from_name(pc, "<Life>k__BackingField") : NULL;
    if (!fLife) fLife = pc ? il2cpp_class_get_field_from_name(pc, "Life") : NULL;
    size_t offLife = fLife ? il2cpp_field_get_offset(fLife) : 0;
    int life0 = offLife ? *(int *)((char *)p0 + offLife) : 8000;
    for (int i = 2; i < upto; i++) {
        void *np = il2cpp_object_new(pc);
        if (!np) continue;
        if (offLife) *(int *)((char *)np + offLife) = life0;
        int key = i;
        void *aa[2] = { &key, np };
        inv(mAdd, dict, aa);
    }
    int n = mCount ? *(int *)il2cpp_object_unbox(inv(mCount, dict, NULL)) : -1;
    nlog("players: dictionary now holds %d, life0=%d (Life at +%zu)", n, life0, offLife);
}

/* The Duel model is not a field of StartDuel: it hangs off the log archive,
   which the manager keeps in a static.  (Hooking Duel.CreatePlayers does not
   work - it is a direct C# call, so it never goes through the MethodInfo.) */
static void *get_duel_model(void) {
    static void *fCur;
    if (!fCur) {
        void *klm = cls(g_img_cs, "", "LogArchiveManager");
        fCur = klm ? il2cpp_class_get_field_from_name(klm, "<CurrentLogArchive>k__BackingField") : NULL;
        if (!fCur) { nlog("model: no CurrentLogArchive field"); return NULL; }
    }
    void *archive = NULL;
    il2cpp_field_static_get_value(fCur, &archive);
    if (!archive) return NULL;
    return *(void **)((char *)archive + 136);      /* <DuelManager>k__BackingField */
}

/* Top the dictionary up to as many players as the layout is showing.  Cheap
   enough to re-check: the duel is rebuilt on reset and on re-entry. */
static void ensure_players(int want) {
    void *duel = get_duel_model();
    if (!duel) return;
    if (duel != g_duelModel) { g_duelModel = duel; g_extraPlayers = 0; }
    void *dict = *(void **)((char *)duel + 16);
    if (!dict) return;
    void *dc = il2cpp_object_get_class(dict);
    void *mCount = dc ? il2cpp_class_get_method_from_name(dc, "get_Count", 0) : NULL;
    void *r = mCount ? inv(mCount, dict, NULL) : NULL;
    int n = r ? *(int *)il2cpp_object_unbox(r) : -1;
    if (n < 0 || n >= want) return;
    add_players(duel, want);
}

static void my_CreatePlayers(void *self, int a, int b, int c, void *mi) {
    if (orig_CreatePlayers) orig_CreatePlayers(self, a, b, c, mi);
    g_duelModel = self;
    nlog("Duel.CreatePlayers(%d, %d, %d)", a, b, c);
    if (g_selMode >= 3) add_players(self, g_selMode >= 4 ? 5 : 4);
    g_extraPlayers = (g_selMode >= 4) ? 5 : (g_selMode >= 3 ? 4 : 2);
}

/* The cloned panels are not wired to anything: the game drives Life01 and
   Life02 from its own two fields and knows nothing about the rest.  So the
   clones get their own wiring - their touch area points at their player index,
   and their number is written from the model. */
static int g_lastLife[5];

static void set_tmp_named(void *tf, const char *name, const char *text, int depth) {
    if (!tf || depth < 0) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    if (!strcmp(nm, name)) set_tmp(tf, text);
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) set_tmp_named(tf_child(tf, i), name, text, depth - 1);
}

/* Which node in a panel actually carries the click, and what it is wired to. */
static void probe_panel_buttons(void *tf, const char *path, int depth) {
    if (!tf || depth < 0) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    char here[192];
    snprintf(here, sizeof here, "%s/%s", path, nm);
    void *b = k_Button ? get_comp(tf, k_Button) : NULL;
    if (b) {
        nlog("probe: Button on %s", here);
        dump_unityevent(fld_obj(b, "m_OnClick"), here);
    }
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) probe_panel_buttons(tf_child(tf, i), here, depth - 1);
}

/* Point a cloned panel at its own player.

   The stock panels do not carry a player index: each one's Button is wired
   straight to CalcBridge.OnKeyLife01 / OnKeyLife02, one method per player, so a
   clone of panel 1 is a second panel 1.  There is a general entry point behind
   those two - StartDuel.OnClickLifePoint(int) - so rewrite the clone's
   persistent call to aim at that with its own index. */
static void *g_startDuel;

static void retarget_panel_click(void *panelTf, int idx) {
    void *b = k_Button ? get_comp(panelTf, k_Button) : NULL;
    if (!b) { nlog("wire: panel %d has no Button", idx); return; }
    void *ev  = fld_obj(b, "m_OnClick");
    void *pcg = ev ? fld_obj(ev, "m_PersistentCalls") : NULL;
    void *lst = pcg ? fld_obj(pcg, "m_Calls") : NULL;
    int  *sz  = lst ? (int *)fld(lst, "_size") : NULL;
    void *items = lst ? fld_obj(lst, "_items") : NULL;
    if (!sz || !*sz || !items) { nlog("wire: panel %d has no persistent call", idx); return; }
    void *pc = ((void **)((char *)items + 32))[0];
    if (!pc) return;
    { static int once; if (!once) { once = 1;
        dump_fields(il2cpp_object_get_class(pc), "PersistentCall"); } }

    void **tgt = (void **)fld(pc, "m_Target");
    void **tan = (void **)fld(pc, "m_TargetAssemblyTypeName");
    void **mn  = (void **)fld(pc, "m_MethodName");
    int   *mode = (int *)fld(pc, "m_Mode");
    void  *ac  = fld_obj(pc, "m_Arguments");
    int   *ia  = ac ? (int *)fld(ac, "m_IntArgument") : NULL;
    if (!tgt || !mn || !mode || !ia) { nlog("wire: panel %d call fields missing", idx); return; }
    if (!g_startDuel) { nlog("wire: no StartDuel instance yet"); return; }
    *tgt  = g_startDuel;
    *mn   = il2cpp_string_new("OnClickLifePoint");
    if (tan) *tan = il2cpp_string_new("StartDuel, Assembly-CSharp");
    *mode = 3;                    /* PersistentListenerMode.Int */
    *ia   = idx;
    uint8_t *dirty = (uint8_t *)fld(ev, "m_CallsDirty");
    if (dirty) *dirty = 1;
    {   /* is the button in a state where it can be pressed at all? */
        void *kb = il2cpp_object_get_class(b);
        void *mEn = kb ? il2cpp_class_get_method_from_name(kb, "get_enabled", 0) : NULL;
        void *mIn = kb ? il2cpp_class_get_method_from_name(kb, "get_interactable", 0) : NULL;
        void *re = mEn ? inv(mEn, b, NULL) : NULL;
        void *ri = mIn ? inv(mIn, b, NULL) : NULL;
        void *btnTf = find_child(panelTf, "btn");
        nlog("wire: panel %d -> StartDuel.OnClickLifePoint(%d) enabled=%d interactable=%d btnVis=%d panelVis=%d",
             idx, idx,
             re ? *(uint8_t *)il2cpp_object_unbox(re) : -1,
             ri ? *(uint8_t *)il2cpp_object_unbox(ri) : -1,
             btnTf ? tf_visible(btnTf) : -1, tf_visible(panelTf));
    }
}

/* Wire every panel, including the game's own two.

   The first two panels were left with the binding they ship with, on the
   principle of touching as little as possible.  That binding goes through
   CalcBridge, which belongs to the two-screen calculator: in four- and
   five-screen mode the call goes nowhere and tapping Duelist 1 or Duelist 2
   simply did nothing.  All five now go through StartDuel.OnClickLifePoint(i),
   which is the game's own entry point and takes the player index. */
static void wire_panels(void) {
    nlog("wire: %d panels to wire", g_np);
    { static int once; if (!once && g_np) { once = 1; probe_panel_buttons(g_panel[0], "", 4); } }
    for (int i = 0; i < g_np; i++) {
        retarget_panel_click(g_panel[i], i);
        g_lastLife[i] = -1;
    }
}

/* Every panel's number comes from the model now, not just the ones the game
   does not know about.  The first two used to be left to the game, but they are
   opened through another player's branch, so the game no longer writes them. */


static void *panel_tween(void *panel);

static void refresh_lives(void) {
    if (g_np < 3) return;
    ensure_players(g_np);
    if (!g_duelModel) return;
    void *kd = il2cpp_object_get_class(g_duelModel);
    void *mGet = kd ? il2cpp_class_get_method_from_name(kd, "GetLife", 1) : NULL;
    if (!mGet) return;
    for (int i = 0; i < g_np; i++) {
        int idx = i;
        void *a[1] = { &idx };
        void *r = inv(mGet, g_duelModel, a);
        if (!r) continue;
        int life = *(int *)il2cpp_object_unbox(r);
        if (life == g_lastLife[i]) continue;
        /* Leave the number alone while the game is counting it up - writing the
           final value every frame would flatten the animation.  But only once
           we have written that panel at least once: on a freshly opened screen
           the tween has never run, reports itself unfinished, and the panel was
           left showing the game's starting life for ever. */
        if (g_lastLife[i] >= 0) {
            void *tw = panel_tween(g_panel[i]);
            void *kt = tw ? il2cpp_object_get_class(tw) : NULL;
            void *mf = kt ? il2cpp_class_get_method_from_name(kt, "IsFinished", 0) : NULL;
            void *rf = mf ? inv(mf, tw, NULL) : NULL;
            if (rf && !*(uint8_t *)il2cpp_object_unbox(rf)) continue;
        }
        g_lastLife[i] = life;
        char buf[16];
        snprintf(buf, sizeof buf, "%d", life);
        set_tmp_named(g_panel[i], "LifePoints", buf, 4);
    }
}

/* Reset and Undo: both are UnityEvent handlers, so they can be hooked.  Reset
   only knows how to put two players back to their starting life, so the rest
   are put back here; the logging also shows whether the click arrives at all. */
static void (*orig_DoReset)(void *, void *);
static void (*orig_Undo)(void *, void *);

static void set_life(int idx, int life) {
    if (!g_duelModel) return;
    void *dict = *(void **)((char *)g_duelModel + 16);
    void *dc = dict ? il2cpp_object_get_class(dict) : NULL;
    void *mGet = dc ? il2cpp_class_get_method_from_name(dc, "get_Item", 1) : NULL;
    if (!mGet) return;
    void *a[1] = { &idx };
    void *pl = inv(mGet, dict, a);
    if (pl) *(int *)((char *)pl + 24) = life;      /* <Life>k__BackingField */
}

static void my_DoReset(void *self, void *mi) {
    nlog("reset: clicked (selected=%d)", *(int *)((char *)self + 44));
    if (orig_DoReset) orig_DoReset(self, mi);
    if (g_np >= 3 && g_duelModel) {
        void *kd = il2cpp_object_get_class(g_duelModel);
        void *mGet = kd ? il2cpp_class_get_method_from_name(kd, "GetLife", 1) : NULL;
        int zero = 0;
        void *a[1] = { &zero };
        void *r = mGet ? inv(mGet, g_duelModel, a) : NULL;
        int base = r ? *(int *)il2cpp_object_unbox(r) : 8000;
        for (int i = 2; i < g_np; i++) set_life(i, base);
        nlog("reset: players 2..%d put back to %d", g_np - 1, base);
    }
}

static void my_Undo(void *self, void *mi) {
    nlog("undo: clicked");
    if (orig_Undo) orig_Undo(self, mi);
}

static void (*orig_StartDuel_Update)(void *, void *);
static void (*orig_StartDuel_OnDisable)(void *, void *);

/* The game re-lays out the panels from its own coroutine after OnEnable, so the
   arrangement has to be re-asserted rather than applied once. */
/* Reset rebuilds the calculator from scratch, and that rebuild looks its pieces
   up by path - the same lookup that came up empty when our wrappers sat where
   Life01 should be, which is what made the whole screen blank on re-entry.  In
   the reset dialog it showed up as a dead Reset button.  So: hand the screen
   back while the dialog is up, and convert it again once the dialog closes. */
static int g_resetOpen;

static int tf_visible(void *tf) {
    if (!tf) return 0;
    if (!m_activeInHierarchy) return 1;
    void *go = inv(m_get_gameObject, tf, NULL);
    void *r  = go ? inv(m_activeInHierarchy, go, NULL) : NULL;
    return r ? *(uint8_t *)il2cpp_object_unbox(r) : 1;
}

/* ---------- the Log dialog ----------

   The dialog is built for two duelists all the way down: a Duelist01 and a
   Duelist02 cell in every row, two name headers, and a LogArchive that stores
   DuelistName1/2 and firstLifePoint01/02.  Only LifeLog.TargetPlayer knows
   about more, which is why a hit on duelist 3 turned up in the Duelist 1 column
   and the running total read 0 - the entry's life snapshot is two wide and the
   player's slot is not in it.

   So the columns get cloned out to five and filled from our own record.  We do
   not need to read the game's entries at all: the list's *length* says when
   something happened, and a snapshot of everyone's life taken at that moment
   says what.  Undo shortens the list and our record follows it down. */
#define LOGMAX 128
static int g_logN;                    /* entries we have a snapshot for */
static int g_logStart[5];             /* what everyone started with */
static int g_logLife[LOGMAX][5];      /* what everyone had after entry i */

static int life_of(int p) {
    if (!g_duelModel) return 0;
    void *kd = il2cpp_object_get_class(g_duelModel);
    void *m  = kd ? il2cpp_class_get_method_from_name(kd, "GetLife", 1) : NULL;
    if (!m) return 0;
    void *a[1] = { &p };
    void *r = inv(m, g_duelModel, a);
    return r ? *(int *)il2cpp_object_unbox(r) : 0;
}

static void *log_arch(void) {
    static void *fCur;
    if (!fCur) {
        void *klm = cls(g_img_cs, "", "LogArchiveManager");
        fCur = klm ? il2cpp_class_get_field_from_name(klm, "<CurrentLogArchive>k__BackingField") : NULL;
        if (!fCur) return NULL;
    }
    void *arch = NULL;
    il2cpp_field_static_get_value(fCur, &arch);
    return arch;
}

static int log_size(void) {
    void *arch = log_arch();
    if (!arch) return -1;
    void *list = *(void **)((char *)arch + 72);      /* <LifeLogs>k__BackingField */
    if (!list) return -1;
    return *(int *)((char *)list + 24);              /* List<T>._size */
}

/* Called every frame the calculator is up.  The newest snapshot is rewritten
   each time rather than taken once, so it does not matter whether the life is
   applied before or after the entry is appended. */
static void archive_id(void *arch, char *out, size_t cap);
static void logdb_save(const char *id);
static void archive_names(void *arch);

static void log_track(void) {
    void *arch = log_arch();
    int n = log_size();
    if (n < 0) return;
    /* The starting life comes from the archive rather than from watching the
       counters: an entry is not appended the instant the sum is applied, and a
       snapshot taken in that gap would record a life that has already moved as
       the life everyone began with. */
    {
        int first = arch ? *(int *)((char *)arch + 144) : 0;   /* firstLifePoint01 */
        if (first < 1) first = 8000;
        for (int p = 0; p < 5; p++) g_logStart[p] = first;
    }
    if (n == 0) { g_logN = 0; return; }
    if (n > LOGMAX) n = LOGMAX;
    if (n < g_logN) g_logN = n;                      /* undo */
    while (g_logN < n) {                             /* new entry: carry forward */
        for (int p = 0; p < 5; p++)
            g_logLife[g_logN][p] = g_logN ? g_logLife[g_logN - 1][p] : g_logStart[p];
        g_logN++;
    }
    for (int p = 0; p < 5; p++) g_logLife[g_logN - 1][p] = life_of(p);

    /* Keep the archive's copy in step.  Written only when something actually
       moved, which is a handful of times a duel. */
    {
        static char lastId[80];
        static int lastN, lastLast;
        char id[80];
        archive_id(arch, id, sizeof id);
        int tail = g_logLife[g_logN - 1][0] + g_logLife[g_logN - 1][1] * 3 +
                   g_logLife[g_logN - 1][2] * 5 + g_logLife[g_logN - 1][3] * 7 +
                   g_logLife[g_logN - 1][4] * 11;
        if (strcmp(id, lastId) || g_logN != lastN || tail != lastLast) {
            snprintf(lastId, sizeof lastId, "%s", id);
            lastN = g_logN; lastLast = tail;
            logdb_save(id);
        }
    }
}


/* how many columns the table wants - four screens get four */
static int log_cols(void) {
    int n = g_np;
    if (n < 2) n = 2;
    if (n > 5) n = 5;
    return n;
}

/* ---------- laying the rows out in five columns ---------- */

/* The game's own two columns are 308 wide in a 620 row, centred at -156 and
   +156 - exactly what this returns for n = 2, so the same call reproduces the
   stock layout and then widens it. */
static void col_geom(int c, int n, float rowW, float *cx, float *sdx) {
    const float gap = 4.0f;
    float w = (rowW - gap * (float)(n - 1)) / (float)n;
    *cx  = -rowW * 0.5f + w * 0.5f + (float)c * (w + gap);
    *sdx = w - rowW;
}

static int name_has(const char *s, const char *frag) {
    size_t n = strlen(frag);
    for (const char *p = s; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && (p[i] | 32) == (frag[i] | 32)) i++;
        if (i == n) return 1;
    }
    return 0;
}

/* which column does this cell belong to?  -1 = not a column cell */
static int cell_col(const char *nm) {
    if (!strncmp(nm, "Duelist01", 9)) return 0;
    if (!strncmp(nm, "Duelist02", 9)) return 1;
    if (!strncmp(nm, "ModCol", 6) && nm[6] >= '2' && nm[6] <= '4') return nm[6] - '0';
    return -1;
}

/* move a cell into its column, leaving everything else about it alone */
static void cell_place(void *tf, int c, int n) {
    void *rt = get_comp(tf, k_RectTransform);
    if (!rt || !m_rt_anchoredPos || !m_rt_sizeDelta) return;
    void *ap = inv(m_rt_anchoredPos, rt, NULL);
    void *sd = inv(m_rt_sizeDelta, rt, NULL);
    if (!ap || !sd) return;
    float *a = (float *)il2cpp_object_unbox(ap);
    float *d = (float *)il2cpp_object_unbox(sd);
    float ay = a[1], dy = d[1];
    float rowW = 0, rowH = 0;
    void *par = inv(meth(k_Transform, "get_parent", 0), tf, NULL);
    if (par) rect_size(par, &rowW, &rowH);
    if (rowW < 1.0f) rowW = 620.0f;
    float cx, sdx;
    col_geom(c, n, rowW, &cx, &sdx);
    V2 nap = { cx, ay }, nsd = { sdx, dy };
    void *ar[1];
    ar[0] = &nsd; inv(m_rt_set_sizeDelta, rt, ar);
    ar[0] = &nap; inv(m_rt_set_anchoredPos, rt, ar);
}

/* let the text shrink to fit a fifth of the row instead of a half */
static void cell_autosize(void *tf) {
    void *c = get_comp(tf, k_TMP);
    if (!c || !m_tmp_autosize || !m_tmp_fsMin || !m_tmp_fsMax || !m_tmp_getFs) return;
    void *r = inv(m_tmp_getFs, c, NULL);
    (void)r;
    /* Let it pick its own size between these.  Handing it the current size as
       the ceiling does not work - with auto-sizing on, that is already the
       fitted size, so the text can only ever shrink from wherever it happens to
       be and the columns come out at different sizes. */
    float mn = 8.0f, mx = 100.0f;
    uint8_t on = 1;
    void *a[1];
    a[0] = &mn; inv(m_tmp_fsMin, c, a);
    a[0] = &mx; inv(m_tmp_fsMax, c, a);
    a[0] = &on; inv(m_tmp_autosize, c, a);
}

/* Walk a heading and everything under it: the skins do not agree on where the
   text component lives - some put it on the cell, some on a child - and reading
   only the cell missed it entirely, which is why the uniform-size pass never
   did anything. */
static void tmp_tree_autosize(void *root) {
    if (!root) return;
    void *stack[16]; int sp = 0; stack[sp++] = root;
    while (sp) {
        void *t = stack[--sp];
        cell_autosize(t);
        int n = tf_children(t);
        for (int j = 0; j < n && sp < 16; j++) stack[sp++] = tf_child(t, j);
    }
}

static float tmp_tree_min_fs(void *root) {
    if (!root || !m_tmp_getFs) return 1e9f;
    float best = 1e9f;
    void *stack[16]; int sp = 0; stack[sp++] = root;
    while (sp) {
        void *t = stack[--sp];
        void *c = get_comp(t, k_TMP);
        if (c) {
            void *r = inv(m_tmp_getFs, c, NULL);
            if (r) {
                float f = *(float *)il2cpp_object_unbox(r);
                if (f > 1.0f && f < best) best = f;
            }
        }
        int n = tf_children(t);
        for (int j = 0; j < n && sp < 16; j++) stack[sp++] = tf_child(t, j);
    }
    return best;
}

static void tmp_tree_fix_fs(void *root, float fs) {
    if (!root || !m_tmp_autosize || !m_tmp_setFs) return;
    void *stack[16]; int sp = 0; stack[sp++] = root;
    while (sp) {
        void *t = stack[--sp];
        void *c = get_comp(t, k_TMP);
        if (c) {
            uint8_t off = 0;
            void *a[1] = { &off };
            inv(m_tmp_autosize, c, a);
            a[0] = &fs; inv(m_tmp_setFs, c, a);
        }
        int n = tf_children(t);
        for (int j = 0; j < n && sp < 16; j++) stack[sp++] = tf_child(t, j);
    }
}

/* Clone the Duelist01 cells three more times.  Everything about a cell - the
   sprite, the colour, the font - comes along with the clone, so the added
   columns are the skin's own artwork rather than something drawn by us. */
static void cols_build(void *grp) {
    void *tpl[8];
    int nt = 0;
    int n = tf_children(grp);
    for (int i = 0; i < n && nt < 8; i++) {
        void *c = tf_child(grp, i);
        char nm[64]; tf_name(c, nm, sizeof nm);
        if (!strncmp(nm, "ModCol", 6)) return;         /* already done */
        if (!strncmp(nm, "Duelist01", 9)) tpl[nt++] = c;
    }
    if (!nt) return;
    for (int c = 2; c < log_cols(); c++)
        for (int i = 0; i < nt; i++) {
            char src[64]; tf_name(tpl[i], src, sizeof src);
            char dst[96]; snprintf(dst, sizeof dst, "ModCol%d_%s", c, src + 9);
            void *go = inv(m_get_gameObject, tpl[i], NULL);
            void *a1[1] = { go };
            void *clone = inv(m_Instantiate, NULL, a1);
            if (!clone) continue;
            void *ctf = inv(m_go_get_transform, clone, NULL);
            uint8_t keep = 0;
            void *sp[2] = { grp, &keep };
            if (ctf) inv(m_SetParent, ctf, sp);
            a1[0] = il2cpp_string_new(dst);
            inv(m_set_name, clone, a1);
        }
}

/* Fill one column of one group.  `text` NULL leaves the text alone; `want` is
   the background variant to show - "plus", "minus", "none" - or NULL to leave
   the backgrounds as they are. */
/* An undone entry carries a back-arrow icon, and it lives *inside* the text
   cell - so the clones inherited one each and every column grew an arrow.  This
   is how a caller says whether this column should keep it: -1 leave alone,
   0 hide, 1 show. */
static void col_fill(void *grp, int c, const char *text, const char *want, int mark) {
    int n = tf_children(grp);
    for (int i = 0; i < n; i++) {
        void *ch = tf_child(grp, i);
        char nm[64]; tf_name(ch, nm, sizeof nm);
        if (cell_col(nm) != c) continue;
        if (name_has(nm, "text")) {
            if (text) set_tmp_tree(ch, text, 2);
            /* No auto-sizing here: a table cell's text rect is zero-height (the
               text overflows it on purpose), and auto-sizing against a
               zero-height box collapses every number to the minimum. */
            if (mark >= 0) {
                int q = tf_children(ch);
                for (int j = 0; j < q; j++) set_active(tf_child(ch, j), mark);
            }
        } else if (want && name_has(nm, "bg")) {
            set_active(ch, name_has(nm, want));
        }
    }
}

/* who moved on entry i of a table, and by how much */
static void tbl_move(const int *start, const int (*life)[5], int i, int *who, int *delta) {
    *who = -1; *delta = 0;
    if (i < 0) return;
    for (int p = 0; p < 5; p++) {
        int before = i ? life[i - 1][p] : start[p];
        int d = life[i][p] - before;
        if (d) { *who = p; *delta = d; return; }
    }
}

/* The game marks an undone row with a back-arrow drawn inside the text cell.
   At a fifth of the row's width the arrow lands on top of the number, and the
   number is the more useful of the two - an undo already reads as a green
   entry putting back exactly what the red one above it took off.  So the arrow
   stays hidden. */

static void group_layout(void *grp) {
    if (!grp) return;
    cols_build(grp);
    int n = tf_children(grp);
    for (int i = 0; i < n; i++) {
        void *ch = tf_child(grp, i);
        char nm[64]; tf_name(ch, nm, sizeof nm);
        int c = cell_col(nm);
        if (c >= 0) cell_place(ch, c, log_cols());
    }
}


/* Fill a life-points table - the same rows whether it is the dialog inside a
   duel or the copy on the saved-log screen, since both are built from the same
   LifeLogRow prefab. */
static void table_rows(void *ct, int nc, const int *start, const int (*lifeTbl)[5], int nEntries) {
    int entry = 0, seenStart = 0;
    int n = tf_children(ct);
    for (int i = 0; i < n; i++) {
        void *row = tf_child(ct, i);
        char rn[64]; tf_name(row, rn, sizeof rn);
        if (!tf_visible(row)) continue;

        if (!strncmp(rn, "Round01", 7)) {              /* the opening life totals */
            seenStart = 1;
            group_layout(row);
            for (int c = 0; c < nc; c++) {
                char t[16]; snprintf(t, sizeof t, "%d", start[c]);
                col_fill(row, c, t, NULL, -1);
            }
            continue;
        }
        if (!strncmp(rn, "Calculate01", 11)) { group_layout(row); continue; }
        if (strncmp(rn, "LifeLogRow", 10)) continue;

        void *calc = find_child(row, "Calculate");
        void *life = find_child(row, "Life");
        /* The first row of the table is a clone too: the totals everyone starts
           the round on, with only its Life half switched on.  A row with no sum
           in it is not an entry, so it must not eat one. */
        if (!calc || !tf_visible(calc)) {
            if (!seenStart && life && tf_visible(life)) {
                seenStart = 1;
                group_layout(life);
                for (int c = 0; c < nc; c++) {
                    char t[16]; snprintf(t, sizeof t, "%d", start[c]);
                    col_fill(life, c, t, NULL, -1);
                }
            }
            continue;
        }
        int idx = entry++;
        if (idx >= nEntries) continue;

        int who, delta;
        tbl_move(start, lifeTbl, idx, &who, &delta);

        if (calc && tf_visible(calc)) {
            group_layout(calc);
            for (int c = 0; c < nc; c++) {
                if (c == who) {
                    char t[24];
                    snprintf(t, sizeof t, "%s %d", delta < 0 ? "-" : "+",
                             delta < 0 ? -delta : delta);
                    col_fill(calc, c, t, delta < 0 ? "minus" : "plus", 0);
                } else {
                    col_fill(calc, c, "", "none", 0);
                }
            }
        }
        if (life && tf_visible(life)) {
            group_layout(life);
            for (int c = 0; c < nc; c++) {
                char t[16]; snprintf(t, sizeof t, "%d", lifeTbl[idx][c]);
                col_fill(life, c, t, NULL, -1);
            }
        }
    }
}


/* ---------- the saved-log screen ----------

   A LogArchive has room for two duelists and no more: two names, two starting
   life points, and entries whose life snapshot is two wide.  There is nothing
   in it to widen, so the five-player table has to be kept beside it, keyed by
   the archive's own LogId, and put back when that duel is opened again. */
#define LOGDB "/data/user/0/jp.konami.YugiohOcgSupports/files/neuronmod.logdb"
#define LOGDB_KEEP 40
/* build.sh drops this marker after installing; the mod clears the saved duels
   on the next start and removes it.  It lives on external storage because the
   shell user cannot reach the app's private directory, where LOGDB is. */
#define WIPE_MARK "/storage/emulated/0/Android/data/jp.konami.YugiohOcgSupports/files/neuronmod.wipe"

static void logdb_wipe_if_asked(void) {
    if (access(WIPE_MARK, F_OK) != 0) return;
    int gone = (unlink(LOGDB) == 0);
    unlink(WIPE_MARK);
    nlog("logdb: fresh build, saved duels %s", gone ? "cleared" : "were already empty");
}

static int  g_arcNp, g_arcN;
static int  g_arcStart[5];
static int  g_arcLife[LOGMAX][5];
static char g_arcNames[5][NAMEMAX];

static void archive_id(void *arch, char *out, size_t cap) {
    out[0] = 0;
    if (arch) cs_str(*(void **)((char *)arch + 168), out, cap);   /* LogId */
}

/* Rewrite the file with this duel's table at the end, dropping any older copy
   of the same duel and anything past the last LOGDB_KEEP duels. */

/* The Log Archives list draws one line per duel out of the archive's own two
   duelist names.  We keep our five elsewhere and put the game's two back after
   every rename, so those fields were left as they came - and the game fills an
   unset name with a Japanese default whose glyphs are not in this build's font,
   which is why every row read as a row of empty boxes with only the date
   legible.  Write the real names in. */
static void archive_names(void *arch) {
    if (!arch || g_selMode < 3) return;
    void *k = il2cpp_object_get_class(arch);
    void *m1 = k ? il2cpp_class_get_method_from_name(k, "set_DuelistName1", 1) : NULL;
    void *m2 = k ? il2cpp_class_get_method_from_name(k, "set_DuelistName2", 1) : NULL;
    if (!m1 || !m2) return;
    char one[NAMEMAX], rest[160];
    snprintf(one, sizeof one, "%s", g_pname[0][0] ? g_pname[0] : "Duelist 1");
    rest[0] = 0;
    for (int i = 1; i < g_np && i < 5; i++) {
        const char *nm = g_pname[i][0] ? g_pname[i] : NULL;
        char buf[NAMEMAX];
        if (!nm) { snprintf(buf, sizeof buf, "Duelist %d", i + 1); nm = buf; }
        if (rest[0] && strlen(rest) + strlen(nm) + 3 < sizeof rest) strcat(rest, ", ");
        if (strlen(rest) + strlen(nm) + 1 < sizeof rest) strcat(rest, nm);
    }
    if (!rest[0]) snprintf(rest, sizeof rest, "Duelist 2");
    void *a[1];
    a[0] = il2cpp_string_new(one);  inv(m1, arch, a);
    a[0] = il2cpp_string_new(rest); inv(m2, arch, a);
}

static void logdb_save(const char *id) {
    if (!id || !id[0] || g_logN <= 0) return;
    char *keep = NULL;
    size_t kept = 0;
    {
        FILE *f = fopen(LOGDB, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0 && sz < 1 << 20) {
                keep = (char *)malloc((size_t)sz + 1);
                if (keep) { kept = fread(keep, 1, (size_t)sz, f); keep[kept] = 0; }
            }
            fclose(f);
        }
    }
    FILE *o = fopen(LOGDB, "w");
    if (!o) { free(keep); nlog("logdb: cannot write %s", LOGDB); return; }
    /* copy the records that are not this duel, keeping the tail */
    if (keep) {
        int records = 0;
        for (char *p = keep; (p = strstr(p, "\nD ")) != NULL; p++) records++;
        int skip = records - (LOGDB_KEEP - 1);
        char *p = keep;
        while (p && *p) {
            char *nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) + 1 : strlen(p);
            if (!strncmp(p, "D ", 2)) {
                char rid[80] = "";
                sscanf(p + 2, "%79s", rid);
                if (!strcmp(rid, id) || skip-- > 0) {         /* drop it and its rows */
                    p += len;
                    while (p && *p && strncmp(p, "D ", 2)) {
                        char *n2 = strchr(p, '\n');
                        p = n2 ? n2 + 1 : p + strlen(p);
                    }
                    continue;
                }
            }
            fwrite(p, 1, len, o);
            p += len;
        }
        free(keep);
    }
    fprintf(o, "D %s %d %d %d\n", id, g_np, g_logN, g_logStart[0]);
    for (int i = 0; i < 5; i++) fprintf(o, "N %s\n", g_pname[i]);
    for (int e = 0; e < g_logN; e++)
        fprintf(o, "E %d %d %d %d %d\n", g_logLife[e][0], g_logLife[e][1],
                g_logLife[e][2], g_logLife[e][3], g_logLife[e][4]);
    fclose(o);
}

static int logdb_load(const char *id) {
    g_arcNp = 0; g_arcN = 0;
    for (int i = 0; i < 5; i++) g_arcNames[i][0] = 0;
    if (!id || !id[0]) return 0;
    FILE *f = fopen(LOGDB, "r");
    if (!f) return 0;
    char line[256];
    int mine = 0, nameIdx = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "D ", 2)) {
            char rid[80] = "";
            int np = 0, n = 0, st = 8000;
            sscanf(line + 2, "%79s %d %d %d", rid, &np, &n, &st);
            mine = !strcmp(rid, id);
            nameIdx = 0;
            if (mine) {
                g_arcNp = np; g_arcN = 0;
                for (int p = 0; p < 5; p++) g_arcStart[p] = st;
                (void)n;
            }
            continue;
        }
        if (!mine) continue;
        if (!strncmp(line, "N ", 2)) {
            if (nameIdx < 5) {
                snprintf(g_arcNames[nameIdx], NAMEMAX, "%s", line + 2);
                char *nl = strchr(g_arcNames[nameIdx], '\n');
                if (nl) *nl = 0;
                nameIdx++;
            }
            continue;
        }
        if (!strncmp(line, "E ", 2) && g_arcN < LOGMAX) {
            int *L = g_arcLife[g_arcN];
            if (sscanf(line + 2, "%d %d %d %d %d", &L[0], &L[1], &L[2], &L[3], &L[4]) == 5)
                g_arcN++;
        }
    }
    fclose(f);
    return g_arcNp > 2 && g_arcN > 0;
}


/* Put a cell into column c of a table whose left and right edges are known in
   world units.  Works whatever the cell is anchored to, which the saved-log
   screen needs: its two headings are stretched inside cells of their own and
   driven by a layout group, so neither their position nor their size means
   anything until that group is switched off. */
static void place_world_column(void *tf, int c, int nc, float left, float right) {
    void *rt = get_comp(tf, k_RectTransform);
    if (!rt || !m_rt_sizeDelta || !il2cpp_array_new || !k_Vector3) return;
    const float gap = 4.0f;
    float tw = right - left;
    if (tw < 1.0f) return;
    float w = (tw - gap * (float)(nc - 1)) / (float)nc;
    float want = left + w * 0.5f + (float)c * (w + gap);

    void *arr = il2cpp_array_new(k_Vector3, 4);
    if (!arr) return;
    void *a[1] = { arr };
    inv(m_GetWorldCorners, rt, a);
    float *f = (float *)((char *)arr + 32);
    float haveW = f[6] - f[0];
    if (haveW < 0) haveW = -haveW;
    float rw = 0, rh = 0;
    rect_size(tf, &rw, &rh);
    if (haveW > 0.001f && rw > 0.001f) {
        float perWorld = rw / haveW;                 /* rect units per world unit */
        void *sd = inv(m_rt_sizeDelta, rt, NULL);
        if (sd) {
            float *d = (float *)il2cpp_object_unbox(sd);
            V2 nsd = { d[0] + (w - haveW) * perWorld, d[1] };
            void *b[1] = { &nsd };
            inv(m_rt_set_sizeDelta, rt, b);
        }
    }
    float cx, cy, cz;
    if (!world_pos(tf, &cx, &cy, &cz)) return;
    float bx, by;
    if (!world_centre(tf, &bx, &by)) return;
    set_world(tf, cx + (want - bx), cy, cz);
}

/* Spread a row of column headers out.

   The two the game ships are the only reliable description of where the table
   is: their own width and the distance between them give the table's width and
   centre, whatever their parent happens to be.  Taking the parent's width
   instead threw the headers across the whole screen, and re-anchoring them to
   the middle dropped them into the middle of the table. */
static void table_headers(void *h1, void *h2, int nc, const char (*names)[NAMEMAX]) {
    if (!h1 || !h2 || nc < 2) return;
    void *parent = tf_parent(h1);
    void *r1 = get_comp(h1, k_RectTransform);
    void *r2 = get_comp(h2, k_RectTransform);
    if (!parent || !r1 || !r2 || !m_rt_anchoredPos || !m_rt_sizeDelta) return;

    void *a1 = inv(m_rt_anchoredPos, r1, NULL);
    void *a2 = inv(m_rt_anchoredPos, r2, NULL);
    void *d1 = inv(m_rt_sizeDelta, r1, NULL);
    if (!a1 || !a2 || !d1) return;
    float x1 = ((float *)il2cpp_object_unbox(a1))[0];
    float y1 = ((float *)il2cpp_object_unbox(a1))[1];
    float x2 = ((float *)il2cpp_object_unbox(a2))[0];
    float sdx1 = ((float *)il2cpp_object_unbox(d1))[0];
    float sdy1 = ((float *)il2cpp_object_unbox(d1))[1];

    float cellW = 0, cellH = 0;
    rect_size(h1, &cellW, &cellH);
    if (cellW < 1.0f || x2 <= x1) return;
    float pw = 0, phh = 0;
    rect_size(parent, &pw, &phh);
    /* sizeDelta is the size outright when the rect is pinned, and an inset when
       it is stretched - tell them apart by whether it matches the real width */
    int stretched = (sdx1 < cellW - 1.0f || sdx1 > cellW + 1.0f);

    const float gap = 4.0f;
    float W   = cellW + (x2 - x1);
    float mid = (x1 + x2) * 0.5f;
    float w   = (W - gap * (float)(nc - 1)) / (float)nc;

    void *hdr[5] = { h1, h2, NULL, NULL, NULL };
    for (int c = 2; c < nc; c++) {
        char nm[32];
        snprintf(nm, sizeof nm, "ModName%d", c);
        void *have = find_child(parent, nm);
        if (!have) {
            void *go = inv(m_get_gameObject, h1, NULL);
            void *ar[1] = { go };
            void *clone = inv(m_Instantiate, NULL, ar);
            if (!clone) continue;
            void *ctf = inv(m_go_get_transform, clone, NULL);
            uint8_t keep = 0;
            void *sp[2] = { parent, &keep };
            if (ctf) inv(m_SetParent, ctf, sp);
            ar[0] = il2cpp_string_new(nm);
            inv(m_set_name, clone, ar);
            have = ctf;
        }
        hdr[c] = have;
    }
    for (int c = 0; c < nc; c++) {
        if (!hdr[c]) continue;
        float cx = mid - W * 0.5f + w * 0.5f + (float)c * (w + gap);
        void *rt = get_comp(hdr[c], k_RectTransform);
        if (rt) {
            V2 nap = { cx, y1 };
            V2 nsd = { stretched ? w - pw : w, sdy1 };
            void *ar[1];
            ar[0] = &nsd; inv(m_rt_set_sizeDelta, rt, ar);
            ar[0] = &nap; inv(m_rt_set_anchoredPos, rt, ar);
        }
        char t[NAMEMAX];
        if (names && names[c][0]) snprintf(t, sizeof t, "%s", names[c]);
        else snprintf(t, sizeof t, "Duelist %d", c + 1);
        set_tmp_tree(hdr[c], t, 3);
        cell_autosize(hdr[c]);
        int q = tf_children(hdr[c]);
        for (int j = 0; j < q; j++) cell_autosize(tf_child(hdr[c], j));
    }
}

/* The dialog, once a frame while it is open. */
static void log_layout(void *ll) {
    void *pop = find_child(ll, "PopUp");
    if (!pop) return;
    void *sv = find_child(pop, "Scroll View");
    if (!sv) return;

    {   /* headers: five names across the top of the table */
        void *h1 = find_child(pop, "Duelist01_name");
        void *h2 = find_child(pop, "Duelist02_name");
        if (!h1 || !h2) return;
        float svW = 0, svH = 0;
        rect_size(sv, &svW, &svH);
        if (svW < 1.0f) svW = 620.0f;
        void *svrt = get_comp(sv, k_RectTransform);
        float svx = 0;
        if (svrt && m_rt_anchoredPos) {
            void *ap = inv(m_rt_anchoredPos, svrt, NULL);
            if (ap) svx = ((float *)il2cpp_object_unbox(ap))[0];
        }
        void *hdr[5] = { h1, h2, NULL, NULL, NULL };
        const int nc = log_cols();
        for (int c = 2; c < nc; c++) {
            char nm[32]; snprintf(nm, sizeof nm, "ModName%d", c);
            void *have = find_child(pop, nm);
            if (!have) {
                void *go = inv(m_get_gameObject, h1, NULL);
                void *a1[1] = { go };
                void *clone = inv(m_Instantiate, NULL, a1);
                if (!clone) continue;
                void *ctf = inv(m_go_get_transform, clone, NULL);
                uint8_t keep = 0;
                void *sp[2] = { pop, &keep };
                if (ctf) inv(m_SetParent, ctf, sp);
                a1[0] = il2cpp_string_new(nm);
                inv(m_set_name, clone, a1);
                have = ctf;
            }
            hdr[c] = have;
        }
        for (int c = 0; c < nc; c++) {
            if (!hdr[c]) continue;
            float cx, sdx;
            col_geom(c, nc, svW, &cx, &sdx);
            void *rt = get_comp(hdr[c], k_RectTransform);
            if (rt) rect_set(rt, 0.5f, 1.0f, 0.5f, 1.0f, 0.5f, 1.0f,
                             svW + sdx, 60.0f, svx + cx, -110.0f);
            if (g_pname[c][0]) {
                set_tmp_tree(hdr[c], g_pname[c], 2);
            } else if (c >= 2) {
                char t[24]; snprintf(t, sizeof t, "Duelist %d", c + 1);
                set_tmp_tree(hdr[c], t, 2);
            }
        }
        /* Auto-sizing fits each heading on its own, so a short name came out at
           twice the size of a long one.  Two things were wrong with the pass
           that was meant to even them up: it read the fitted size on the very
           frame it asked for the fit, before TMP had recomputed anything, and
           it read it off the heading's own transform, where several skins keep
           no text at all.  Let the fit run for a few frames, take the smallest
           size any heading settled on, then pin every one of them to it. */
        {
            char sig[320];
            size_t sl = 0;
            sl += (size_t)snprintf(sig + sl, sizeof sig - sl, "%d", nc);
            for (int c = 0; c < nc && sl < sizeof sig - 1; c++)
                sl += (size_t)snprintf(sig + sl, sizeof sig - sl, "|%s", g_pname[c]);
            static char  lastSig[320];
            static int   measure;
            static float lockFs;
            if (strcmp(sig, lastSig)) {
                snprintf(lastSig, sizeof lastSig, "%s", sig);
                measure = 5;
                lockFs = 0.0f;
            }
            if (measure > 0) {
                for (int c = 0; c < nc; c++) tmp_tree_autosize(hdr[c]);
                if (--measure == 0) {
                    float small = 1e9f;
                    for (int c = 0; c < nc; c++) {
                        float f = tmp_tree_min_fs(hdr[c]);
                        if (f < small) small = f;
                    }
                    if (small < 1e8f) lockFs = small;
                    nlog("log: heading font %.1f across %d columns", lockFs, nc);
                }
            } else if (lockFs > 1.0f) {
                for (int c = 0; c < nc; c++) tmp_tree_fix_fs(hdr[c], lockFs);
            }
        }
    }

    void *vp = find_child(sv, "Viewport");
    void *ct = vp ? find_child(vp, "Content") : NULL;
    if (!ct) return;

    table_rows(ct, log_cols(), g_logStart, g_logLife, g_logN);
}

/* Reach a player object, so its life and name can be set outright.  AddLife
   would do the arithmetic but it also writes history, and restoring a duel is
   not a move anyone made. */
static void *player_at(int i) {
    void *duel = g_duelModel ? g_duelModel : get_duel_model();
    if (!duel) return NULL;
    void *dict = *(void **)((char *)duel + 16);
    if (!dict) return NULL;
    void *dc = il2cpp_object_get_class(dict);
    void *mHas = dc ? il2cpp_class_get_method_from_name(dc, "ContainsKey", 1) : NULL;
    void *mGet = dc ? il2cpp_class_get_method_from_name(dc, "get_Item", 1) : NULL;
    if (!mGet) return NULL;
    int key = i;
    void *a[1] = { &key };
    if (mHas) {
        void *r = inv(mHas, dict, a);
        if (!r || !*(uint8_t *)il2cpp_object_unbox(r)) return NULL;
    }
    return inv(mGet, dict, a);
}

static void player_set_life(int i, int life) {
    void *p = player_at(i);
    if (!p) return;
    void *pc = il2cpp_object_get_class(p);
    void *f = pc ? il2cpp_class_get_field_from_name(pc, "<Life>k__BackingField") : NULL;
    if (!f) return;
    *(int *)((char *)p + il2cpp_field_get_offset(f)) = life;
}

static void player_set_name(int i, const char *name) {
    void *p = player_at(i);
    if (!p || !name || !name[0]) return;
    void *pc = il2cpp_object_get_class(p);
    void *f = pc ? il2cpp_class_get_field_from_name(pc, "Name") : NULL;
    if (!f) return;
    *(void **)((char *)p + il2cpp_field_get_offset(f)) = il2cpp_string_new(name);
}

/* Put a duel back the way it was.

   Changing the Calculator Design tears the screen down and builds it again, and
   whatever the game does to the model in between, the five life points are ours
   to keep: they are written back from the table we already save against the
   archive's LogId.  The same path restores a duel picked out of Log Archives. */
static int  g_restorePending;
static char g_restoreId[80];

static void restore_duel(void) {
    if (!g_restorePending) return;
    char id[80];
    if (g_restoreId[0]) snprintf(id, sizeof id, "%s", g_restoreId);
    else archive_id(log_arch(), id, sizeof id);
    if (!logdb_load(id) || g_arcN <= 0) { g_restorePending = 0; return; }
    ensure_players(g_arcNp > 5 ? 5 : g_arcNp);
    for (int p = 0; p < 5 && p < g_arcNp; p++) {
        player_set_life(p, g_arcLife[g_arcN - 1][p]);
        /* Not player_set_name: the panels bind a field of their own to
           Duel.Player.Name, and writing there put a second, much narrower copy
           of every name on the plate beside the caption we draw ourselves. */
        if (g_arcNames[p][0]) snprintf(g_pname[p], NAMEMAX, "%s", g_arcNames[p]);
        g_lastLife[p] = -1;
    }
    for (int p = 0; p < 5; p++) g_logStart[p] = g_arcStart[p];
    g_logN = g_arcN;
    for (int e = 0; e < g_arcN; e++)
        for (int p = 0; p < 5; p++) g_logLife[e][p] = g_arcLife[e][p];
    g_nameDirty = 1;
    g_restorePending = 0;
    g_restoreId[0] = 0;
    nlog("restore: duel '%s' put back - %d players, %d entries", id, g_arcNp, g_arcN);
}

static void (*orig_ClickLife)(void *, int, void *);

/* Tapping a duelist panel.

   StartDuel.OnClickLifePoint(i) opens the keypad for player i, and it works for
   every index except the first two, where it throws a NullReferenceException
   before the keypad appears - so Duelist 1 and Duelist 2 could not be edited at
   all.  The reason is the mode masking: the mod lets the game build its normal
   two-screen calculator and converts the result, and the branch this method
   takes for the two players the two-screen layout knows about reaches for
   widgets that only the single-screen layouts fill in.  Filling those in by hand
   does not help - something it looks up is missing too.

   The branch for any *other* index does the whole job and does it correctly, so
   the first two go in through that door and are pointed back at their own player
   the moment it is open.  The keypad's own header is written from the model
   anyway, so nothing downstream can tell the difference. */
/* Hand the game the widgets it animates through.

   The branch we go in by never fills SelectedPlayerLifeText or
   SelectedPlayerTween, so when the sum lands the game has nothing to count up
   and nothing to play a sound over - the number just changed.  Point those at
   the panel that is actually being edited and the game's own count animation
   and its sound come back. */
static void *panel_tween(void *panel) {
    static void *kTw;
    if (!kTw) kTw = il2cpp_class_from_name(g_img_cs, "UISystem", "TweenCounterTmp");
    if (!kTw || !panel) return NULL;
    void *stack[48]; int sp = 0;
    stack[sp++] = panel;
    while (sp) {
        void *t = stack[--sp];
        void *c = get_comp(t, kTw);
        if (c) return c;
        int n = tf_children(t);
        for (int j = 0; j < n && sp < 48; j++) stack[sp++] = tf_child(t, j);
    }
    return NULL;
}

static void aim_animation(void *self, int idx) {
    if (idx < 0 || idx >= g_np || !g_panel[idx]) return;
    void *lp = find_deep(g_panel[idx], "LifePoints", 4);
    if (!lp) return;
    void *lpT = get_comp(lp, k_TMP);
    /* The count-up needs the number's shadow copy, and taking the first text
       child that happened to be there grabbed ZEXAL's 'LP' caption instead: the
       game then animated the countdown into the caption, in red, in a rect too
       narrow for four digits, so it wrapped across two lines on top of the
       panel.  Only take a child that really is the shadow - named for it, or
       already showing the same string as the number itself. */
    void *shadow = NULL;
    char cur[32] = "";
    if (lpT && m_get_text) cs_str(inv(m_get_text, lpT, NULL), cur, sizeof cur);
    for (int i = 0, n = tf_children(lp); i < n && !shadow; i++) {
        void *c = tf_child(lp, i);
        void *ct = get_comp(c, k_TMP);
        if (!ct) continue;
        char nm[64]; tf_name(c, nm, sizeof nm);
        if (strstr(nm, "Shadow") || strstr(nm, "shadow")) { shadow = c; break; }
        char t[32] = "";
        if (m_get_text) cs_str(inv(m_get_text, ct, NULL), t, sizeof t);
        if (cur[0] && !strcmp(t, cur)) shadow = c;
    }
    void *shT = shadow ? get_comp(shadow, k_TMP) : lpT;
    /* A GameObject of our own to absorb whatever the game switches on and off
       around the shadow, so it cannot hide the number itself. */
    void *dummy = find_child(g_panel[idx], "ModAnimSink");
    if (!dummy) { void *rt = new_rect(g_panel[idx], "ModAnimSink"); dummy = rt; }
    void *dummyGo = dummy ? inv(m_get_gameObject, dummy, NULL) : NULL;
    void *shGo = shadow ? inv(m_get_gameObject, shadow, NULL) : dummyGo;
    *(void **)((char *)self + 56)  = lpT;                 /* SelectedPlayerLifeText */
    *(void **)((char *)self + 64)  = shT;                 /* ...Shadow */
    *(void **)((char *)self + 72)  = shGo;                /* ...ShadowObj1 */
    *(void **)((char *)self + 80)  = dummyGo;             /* ...ShadowObj2 */
    void *tw = panel_tween(g_panel[idx]);
    *(void **)((char *)self + 344) = tw;
    /* The tween keeps its own pointer to the text it counts in, and on ZEXAL it
       had latched onto the panel's 'LP' caption: the running total played there
       in red, in a rect two digits wide, so it wrapped across the plate - and
       stayed there, because nothing ever wrote the caption back.  Point it at
       the number itself, every frame, since it is re-latched when a count
       starts. */
    if (tw && lpT) *(void **)((char *)tw + 152) = lpT;
}

/* Put back anything the count animation scribbled on.

   Whatever the tween had already written before we could re-point it stays on
   screen for good, so remember what each panel's text nodes said before any
   arithmetic happened and restore any that comes back wearing colour markup. */
#define SCRUBMAX 8
static void *g_scrubFor[5];
static void *g_scrubTf[5][SCRUBMAX];
static char  g_scrubTxt[5][SCRUBMAX][32];
static int   g_scrubN[5];

static void scrub_collect(void *tf, int idx, int depth, void *skip) {
    if (!tf || depth < 0 || g_scrubN[idx] >= SCRUBMAX) return;
    void *c = get_comp(tf, k_TMP);
    if (c && c != skip && m_get_text) {
        char t[32] = "";
        cs_str(inv(m_get_text, c, NULL), t, sizeof t);
        if (!strstr(t, "<color=")) {
            g_scrubTf[idx][g_scrubN[idx]] = c;
            snprintf(g_scrubTxt[idx][g_scrubN[idx]], 32, "%s", t);
            g_scrubN[idx]++;
        }
    }
    for (int i = 0, n = tf_children(tf); i < n; i++)
        scrub_collect(tf_child(tf, i), idx, depth - 1, skip);
}

static void scrub_panels(void) {
    if (g_selMode < 3 || !m_get_text || !m_set_text) return;
    for (int i = 0; i < g_np && i < 5; i++) {
        void *p = g_panel[i];
        if (!p) continue;
        void *lp = find_deep(p, "LifePoints", 4);
        void *lpT = lp ? get_comp(lp, k_TMP) : NULL;
        if (p != g_scrubFor[i]) {
            g_scrubFor[i] = p;
            g_scrubN[i] = 0;
            scrub_collect(p, i, 5, lpT);
        }
        {   /* TweenCounterTmp counts into whatever its 'text' field points at,
               and the game re-points that at the panel's 'LP' caption every time
               a count starts - a rect two digits wide, so the running total
               wrapped across the plate in red and stayed there.  Aiming it once
               when the panel is tapped is too early: the count is started later,
               on the way out of the keypad.  Re-point it here, every frame, for
               every panel. */
            void *tw = panel_tween(p);
            if (tw && lpT) *(void **)((char *)tw + 152) = lpT;
        }
        static int  held[5][SCRUBMAX];
        static char prev[5][SCRUBMAX][48];
        for (int k = 0; k < g_scrubN[i]; k++) {
            void *c = g_scrubTf[i][k];
            if (!c) continue;
            char t[48] = "";
            cs_str(inv(m_get_text, c, NULL), t, sizeof t);
            if (!strstr(t, "<color=")) { held[i][k] = 0; prev[i][k][0] = 0; continue; }
            /* Colour markup on a caption is a count-up that landed in the wrong
               place.  Give it a few frames in case it is still running, then put
               the caption back - which is what never happened before, leaving a
               red total sitting on the plate for the rest of the duel. */
            if (strcmp(t, prev[i][k])) {
                snprintf(prev[i][k], 48, "%s", t);
                held[i][k] = 0;
                continue;
            }
            if (++held[i][k] < 8) continue;
            void *a[1] = { il2cpp_string_new(g_scrubTxt[i][k]) };
            inv(m_set_text, c, a);
            held[i][k] = 0;
            prev[i][k][0] = 0;
        }
    }
}

static void my_ClickLife(void *self, int idx, void *mi) {
    if (!orig_ClickLife) return;
    if (g_selMode >= 3 && idx < 2 && g_np > 2) {
        orig_ClickLife(self, 2, mi);
        *(int *)((char *)self + 44) = idx;      /* SelectedPlayerIndex */
    } else {
        orig_ClickLife(self, idx, mi);
    }
    if (g_selMode >= 3) aim_animation(self, idx);
}

/* Closing the keypad once the sum is in.

   In the two-screen layout the keypad and the panels share one screen, so there
   is nothing to close and the game never does.  Ours is a separate screen, and
   leaving it up after '=' means the change you just made is out of sight - so
   press Back for the user, the same call the arrow makes.  The panel then plays
   the game's own count animation on the way in. */
/* Closing the keypad once the sum is in.

   In the two-screen layout the keypad shares the screen with the panels, so
   there is nothing to close and the game never does.  Ours is a screen of its
   own, and leaving it up after '=' hides the very change you just made.

   Hooking the '=' key itself does not work - the key is a UnityEvent on
   CalcBridge and the patched entry never runs - so watch the result instead:
   the moment the game appends an entry to its own life log while the keypad is
   up, the sum has landed, and Back is pressed for the user.  The panel then
   plays the game's own count-up as it comes into view. */
static void close_keypad_if_done(void *self) {
    if (g_selMode < 3 || !g_duelNode) return;
    static int seen = -1;
    void *multi = *(void **)((char *)self + 176);
    void *mtf = multi ? inv(m_go_get_transform, multi, NULL) : NULL;
    void *calc = mtf ? find_deep(mtf, "Calculator", 2) : NULL;
    int open = tf_visible(calc);
    if (!open) { seen = g_logN; return; }
    if (seen < 0) { seen = g_logN; return; }
    if (g_logN <= seen) return;
    seen = g_logN;
    void *k = il2cpp_object_get_class(self);
    void *back = k ? il2cpp_class_get_method_from_name(k, "OnClickBack", 0) : NULL;
    nlog("keypad: sum applied, closing");
    if (back) inv(back, self, NULL);
}


/* Log Archives list: each row shows empty boxes where the duel time belongs,
   while the date beside it renders - so the time is drawn with a font whose
   glyphs are not there.  Log what each row holds and which font it asks for. */
static void dump_row_fonts(void *tf, const char *path, int depth) {
    if (!tf || depth < 0) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    char here[192];
    snprintf(here, sizeof here, "%s/%s", path, nm);
    void *c = get_comp(tf, k_TMP);
    if (c && m_get_text) {
        char t[80] = "";
        cs_str(inv(m_get_text, c, NULL), t, sizeof t);
        if (t[0]) {
            char fn[64] = "?";
            void *fa = fld(c, "m_fontAsset");
            void *font = fa ? *(void **)fa : NULL;
            if (font && m_get_name) cs_str(inv(m_get_name, font, NULL), fn, sizeof fn);
            nlog("row %-52s '%s' font '%s'", here, t, fn);
        }
    }
    for (int i = 0, n = tf_children(tf); i < n; i++)
        dump_row_fonts(tf_child(tf, i), here, depth - 1);
}

/* Every duel we saved carries a name line the game filled with its own
   Japanese default, and those glyphs are not in this build's font - so the list
   read as rows of empty boxes.  Anything non-ASCII in that cell is that
   default; put the duelists' names there instead. */
static void fix_row_labels(void *tf, int depth) {
    if (!tf || depth < 0) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    if (!strcmp(nm, "PlayerName")) {
        void *c = get_comp(tf, k_TMP);
        if (c && m_get_text) {
            void *sobj = inv(m_get_text, c, NULL);
            int odd = 0;
            if (sobj) {
                int len = *(int *)((char *)sobj + 16);
                const uint16_t *u = (const uint16_t *)((char *)sobj + 20);
                for (int i = 0; i < len; i++) if (u[i] > 126) { odd = 1; break; }
            }
            if (odd) {
                char line[220] = "";
                for (int i = 0; i < 5; i++) {
                    if (!g_pname[i][0]) continue;
                    if (line[0] && strlen(line) + 3 < sizeof line) strcat(line, ", ");
                    if (strlen(line) + strlen(g_pname[i]) + 1 < sizeof line)
                        strcat(line, g_pname[i]);
                }
                if (line[0]) set_tmp(tf, line);
            }
        }
    }
    for (int i = 0, n = tf_children(tf); i < n; i++)
        fix_row_labels(tf_child(tf, i), depth - 1);
}

/* The rows show only a date.  The archive knows the minute the duel started, so
   put the time beside it - the rows are otherwise indistinguishable when you
   play several duels in a day.

   DateTime is a struct of ticks (100ns since year 1) with its kind in the top
   two bits, so the clock can be read straight out of it without going anywhere
   near string formatting. */
static void row_add_time(void *row, void *arch) {
    if (!row || !arch) return;
    void *k = il2cpp_object_get_class(arch);
    void *m = k ? il2cpp_class_get_method_from_name(k, "get_DuelStartDate", 0) : NULL;
    if (!m) return;
    void *r = inv(m, arch, NULL);
    if (!r) return;
    uint64_t ticks = (*(uint64_t *)il2cpp_object_unbox(r)) & 0x3FFFFFFFFFFFFFFFULL;
    if (!ticks) return;
    int hh = (int)((ticks / 36000000000ULL) % 24);
    int mm = (int)((ticks / 600000000ULL) % 60);
    void *cell = find_deep(row, "Updated", 3);
    void *c = cell ? get_comp(cell, k_TMP) : NULL;
    if (!c || !m_get_text) return;
    char cur[64] = "";
    cs_str(inv(m_get_text, c, NULL), cur, sizeof cur);
    if (!cur[0] || strchr(cur, ':')) return;      /* already carries a time */
    char out[80];
    snprintf(out, sizeof out, "%s %02d:%02d", cur, hh, mm);
    set_tmp(cell, out);
}

/* The archives, in the order the list draws them. */
static void *archive_at(int i);

static void stamp_rows(void *root) {
    void *list = find_deep(root, "LogList", 5);
    if (!list) return;
    for (int i = 0, n = tf_children(list); i < n; i++)
        row_add_time(tf_child(list, i), archive_at(i));
}

static void *archive_at(int i) {
    static void *fld_list;
    if (!fld_list) {
        void *klm = cls(g_img_cs, "", "LogArchiveManager");
        if (!klm) return NULL;
        fld_list = il2cpp_class_get_field_from_name(klm, "<LogArchives>k__BackingField");
        if (!fld_list) { nlog("archives: no LogArchives field"); return NULL; }
    }
    void *lst = NULL;
    il2cpp_field_static_get_value(fld_list, &lst);
    if (!lst) return NULL;
    int n = *(int *)((char *)lst + 24);
    if (i < 0 || i >= n) return NULL;
    void *arr = *(void **)((char *)lst + 16);
    if (!arr) return NULL;
    return ((void **)((char *)arr + 32))[i];
}

static void *list_root(void *self) {
    void *k = il2cpp_object_get_class(self);
    void *mtf = k ? il2cpp_class_get_method_from_name(k, "get_transform", 0) : NULL;
    return mtf ? inv(mtf, self, NULL) : NULL;
}

static void (*orig_LogList_OnEnable)(void *, void *);
static void my_LogList_OnEnable(void *self, void *mi) {
    if (orig_LogList_OnEnable) orig_LogList_OnEnable(self, mi);
    void *tf = list_root(self);
    if (tf) { fix_row_labels(tf, 8); stamp_rows(tf); }
}

static void (*orig_DisplayLogList)(void *, void *);
static void my_DisplayLogList(void *self, void *mi) {
    if (orig_DisplayLogList) orig_DisplayLogList(self, mi);
    void *tf = list_root(self);
    if (tf) { fix_row_labels(tf, 8); stamp_rows(tf); }
}


/* The footer's four other tabs open menus that have nothing to do with the
   calculator, and this build is a calculator.  Leave Duel alone and take the
   rest out of service. */
static void trim_one_footer(void *tf) {
    if (!tf) return;
    /* Keep the tab whose label reads Duel; switch the rest off outright, which
       takes their menus with them - nothing can reach a screen it cannot tap
       its way to. */
    /* The tabs are Topic / Duel / Deck / Event / Data, in that order.  Match on
       the node name, not the caption: the captions arrive from the localiser
       long after this runs, and not on the tab itself but on a child.  Exact
       names only - the footer also carries DeckAdd, DeckCount and friends,
       which belong to deck editing and are nothing to do with the tabs. */
    static const char *drop[] = { "Topic", "Deck", "Event", "Data" };
    for (int i = 0, n = tf_children(tf); i < n; i++) {
        void *ch = tf_child(tf, i);
        char nm[64]; tf_name(ch, nm, sizeof nm);
        for (int d = 0; d < 4; d++) {
            if (strcmp(nm, drop[d])) continue;
            /* Switching the object off is not enough on its own - the footer
               turns its tabs back on when it rebuilds - so also take the button
               out of service and collapse the tab to nothing, neither of which
               it ever puts back. */
            int wasOn = tf_visible(ch);
            set_active(ch, 0);
            void *bt = get_comp(ch, k_Button);
            if (bt && m_set_enabled) { uint8_t off = 0; void *ea[1] = { &off }; inv(m_set_enabled, bt, ea); }
            if (m_set_localScale) { V3 z = { 0.0f, 0.0f, 0.0f }; void *sa[1] = { &z }; inv(m_set_localScale, ch, sa); }
            nlog("footer: '%s' removed (was %s)", nm, wasOn ? "on" : "off");
            break;
        }
    }
}

/* There is more than one Footer in the scene and the visible one had already
   come up before the hooks were in place, so trimming the instance that called
   us left the bar on screen untouched.  Walk out to the root and trim every
   Footer there is. */
static void trim_all_footers(void *tf, int depth) {
    if (!tf || depth < 0) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    if (!strcmp(nm, "Footer")) { trim_one_footer(tf); return; }
    for (int i = 0, n = tf_children(tf); i < n; i++)
        trim_all_footers(tf_child(tf, i), depth - 1);
}

static void footer_trim(void *self) {
    void *tf = list_root(self);
    /* Walking out to the root only reaches the hierarchy the caller belongs to,
       and the bar you can see lives in another one - so ask the runtime for
       every Footer there is, whichever scene it sits in. */
    void *kf = cls(g_img_cs, "", "Footer");
    void *kres = g_img_core ? cls(g_img_core, "UnityEngine", "Resources") : NULL;
    void *mfind = kres ? meth(kres, "FindObjectsOfTypeAll", 1) : NULL;
    if (!kf || !mfind) { if (tf) trim_all_footers(tf, 10); return; }
    void *a[1] = { il2cpp_type_get_object(il2cpp_class_get_type(kf)) };
    void *arr = inv(mfind, NULL, a);
    if (!arr) { if (tf) trim_all_footers(tf, 10); return; }
    int n = (int)(*(uintptr_t *)((char *)arr + 24));
    void **el = (void **)((char *)arr + 32);
    {
        static int said;
        if (said != n) { said = n; nlog("footer: %d Footer objects in the scene", n); }
    }
    for (int i = 0; i < n && i < 32; i++) {
        if (!el[i]) continue;
        trim_one_footer(list_root(el[i]));
    }
}

/* Switching them off once is not enough - the footer puts its tabs back
   whenever it rebuilds itself for a new screen, so trim after each of the calls
   that does the rebuilding. */
static void (*orig_Footer_OnEnable)(void *, void *);
static void my_Footer_OnEnable(void *self, void *mi) {
    if (orig_Footer_OnEnable) orig_Footer_OnEnable(self, mi);
    footer_trim(self);
}
static void (*orig_Footer_Default)(void *, void *);
static void my_Footer_Default(void *self, void *mi) {
    if (orig_Footer_Default) orig_Footer_Default(self, mi);
    footer_trim(self);
}
static void (*orig_Footer_Adapt)(void *, void *);
static void my_Footer_Adapt(void *self, void *mi) {
    if (orig_Footer_Adapt) orig_Footer_Adapt(self, mi);
    footer_trim(self);
}
static void (*orig_Footer_Start)(void *, void *);
static void my_Footer_Start(void *self, void *mi) {
    if (orig_Footer_Start) orig_Footer_Start(self, mi);
    footer_trim(self);
}

static void my_StartDuel_Update(void *self, void *mi) {
    g_startDuel = self;
    if (orig_StartDuel_Update) orig_StartDuel_Update(self, mi);
    if (g_selMode >= 3) {
        if (g_duelNode) {
            int open = tf_visible(find_child(g_duelNode, "ConfirmReset"));
            if (open && !g_resetOpen) {
                g_resetOpen = 1;
                nlog("reset dialog open: handing the screen back");
                mod_teardown(self);
            } else if (!open && g_resetOpen) {
                g_resetOpen = 0;
                nlog("reset dialog closed: converting again");
            }
        }
        if (g_resetOpen) return;
        restore_duel();
        log_track();
        close_keypad_if_done(self);
        /* Re-aim every frame.  Aiming once, at the moment the panel is tapped,
           was not enough: the game re-binds these widgets on its way back out of
           the keypad, so the count-up played on whatever the two-screen layout
           had pointed them at. */
        {
            int sel = *(int *)((char *)self + 44);
            if (sel >= 0 && sel < g_np) aim_animation(self, sel);
        }
        scrub_panels();
        watch_rename(self);
        {   /* the Log dialog, while it is up */
            void *ll = g_duelNode ? find_child(g_duelNode, "LifeLog") : NULL;
            if (tf_visible(ll)) log_layout(ll);
        }
        build_four_player_layout(self);   /* no-op once wrapped */
        settle_buttons();
        settle_panels();
        refresh_lives();
        fix_keypad_header(self);
    }
}

/* The keypad header is wired to the two players the game ships with: it shows
   duelist 2's name and duelist 2's life for anything past index 1.  The model
   is right - only this readout is stale - so refresh it from the model while a
   later player is selected. */
static void dump_texts(void *tf, const char *path, int depth) {
    if (!tf || depth < 0) return;
    char nm[64]; tf_name(tf, nm, sizeof nm);
    char here[192];
    snprintf(here, sizeof here, "%s/%s", path, nm);
    void *c = k_TMP ? get_comp(tf, k_TMP) : NULL;
    if (c && m_get_text) {
        char t[80];
        cs_str(inv(m_get_text, c, NULL), t, sizeof t);
        if (t[0]) nlog("txt: %-60s '%s'", here, t);
    }
    int n = tf_children(tf);
    for (int i = 0; i < n; i++) dump_texts(tf_child(tf, i), here, depth - 1);
}

static void fix_keypad_header(void *self) {
    if (!self || !g_duelModel) return;
    int idx = *(int *)((char *)self + 44);          /* SelectedPlayerIndex */
    /* Every index now, not just the ones the game does not know about: the first
       two reach the keypad through another player's branch, so their header is
       as much ours to write as anyone's. */
    if (g_selMode < 3 || idx < 0 || idx >= g_np) return;
    {   /* once: what is behind the keys we need to react to? */
        static int once;
        if (!once) {
            once = 1;
            void *go = *(void **)((char *)self + 216);   /* CurrentCalculator */
            if (!go) go = *(void **)((char *)self + 144);
            void *tf = go ? inv(m_go_get_transform, go, NULL) : NULL;
            const char *keys[] = { "Equal", "Sub", "Add" };
            for (int i = 0; i < 3; i++) {
                void *b = tf ? find_deep(tf, keys[i], 8) : NULL;
                void *btn = b ? (k_Button ? get_comp(b, k_Button) : NULL) : NULL;
                if (!btn && b) { void *c = find_deep(b, "btn", 2); btn = c && k_Button ? get_comp(c, k_Button) : NULL; }
                nlog("keypad: %s tf=%p button=%p", keys[i], b, btn);
                if (btn) dump_unityevent(fld_obj(btn, "m_OnClick"), keys[i]);
            }
        }
    }
    void *kd = il2cpp_object_get_class(g_duelModel);
    void *mGet = kd ? il2cpp_class_get_method_from_name(kd, "GetLife", 1) : NULL;
    if (!mGet) return;
    void *a[1] = { &idx };
    void *r = inv(mGet, g_duelModel, a);
    if (!r) return;
    int life = *(int *)il2cpp_object_unbox(r);

    /* The name is the placeholder of the rename field, and the life is the
       leading number of the equation line - both come from the game's cache of
       *two* players, so for anything past index 1 they show duelist 2's. */
    void *inf = *(void **)((char *)self + 256);          /* DuelistNameIf */
    void *ph  = inf ? fld_obj(inf, "m_Placeholder") : NULL;
    if (ph && m_set_text) {
        char nm[NAMEMAX];
        if (g_pname[idx][0]) snprintf(nm, sizeof nm, "%s", g_pname[idx]);
        else snprintf(nm, sizeof nm, "Duelist %d", idx + 1);
        void *ta[1] = { il2cpp_string_new(nm) };
        inv(m_set_text, ph, ta);
    }
    void *eq = *(void **)((char *)self + 248);           /* EquationText */
    if (eq && m_get_text && m_set_text) {
        char cur[96];
        cs_str(inv(m_get_text, eq, NULL), cur, sizeof cur);
        int a0 = 0;
        while (cur[a0] == ' ') a0++;
        int a1 = a0;
        while (cur[a1] >= '0' && cur[a1] <= '9') a1++;
        if (a1 > a0) {
            char want[96];
            snprintf(want, sizeof want, "%.*s%d%s", a0, cur, life, cur + a1);
            if (strcmp(want, cur)) {
                void *ta[1] = { il2cpp_string_new(want) };
                inv(m_set_text, eq, ta);
            }
        }
    }
}

static void my_StartDuel_OnDisable(void *self, void *mi) {
    if (g_selMode >= 3) {          /* the game may add an entry as the duel ends */
        char id[80];
        void *arch = log_arch();
        archive_id(arch, id, sizeof id);
        logdb_save(id);
    }
    /* the teardown also keys off g_CalcMode, so it has to run while still masked */
    if (orig_StartDuel_OnDisable) orig_StartDuel_OnDisable(self, mi);
    if (g_selMode >= 3) write_calc_mode(g_selMode);
    g_wrappedArea = NULL;
    g_row = NULL; g_settle = 0;
    nlog("StartDuel.OnDisable: g_CalcMode restored to %d", g_selMode);
}

static void my_StartDuel_OnEnable(void *self, void *mi) {
    /* StartDuel switches on g_CalcMode to choose a calculator canvas and knows
       nothing about 3/4, so let it build the normal 2-screen layout, then convert. */
    mod_teardown(self);          /* the game is about to walk this hierarchy */
    int mode = read_calc_mode();
    g_selMode = mode;            /* 2 players means 2 players again, stock layout */
    if (mode >= 3) write_calc_mode(0);   /* stays masked until OnDisable */
    if (orig_StartDuel_OnEnable) orig_StartDuel_OnEnable(self, mi);
    nlog("StartDuel.OnEnable: g_CalcMode=%d masked to 0 for the duration", mode);
    if (g_selMode >= 3) {
        g_restorePending = 1;
        /* Leaving to the menu and coming back keeps the model - the keypad still
           showed 7500 - but the game repaints every panel with the starting
           life, and refresh_lives only writes a number when it differs from the
           one it last wrote, so a panel already told 7500 was left showing the
           game's 8000.  Forget what was last written every time the screen
           comes up.  This has to sit above the one-shot dump below, which used
           to swallow everything after it on the second visit. */
        for (int i = 0; i < 5; i++) g_lastLife[i] = -1;
    }
    if (g_duel_dumped) return;
    g_duel_dumped = 1;
    nlog("=== StartDuel.OnEnable: mapping the calculator ===");
    {   /* the life-point model: Duel/Player look player-indexed all the way
           through (GetLife(i), AddLife(i,v), GetHistory(i), LifeLog.TargetPlayer),
           so five independent calculators may only need five Players and the
           panel buttons retargeted */
        void *kd  = cls(g_img_cs, "", "Duel");
        void *kla = cls(g_img_cs, "", "LogArchive");
        void *klm = cls(g_img_cs, "", "LogArchiveManager");
        if (kd)  dump_fields(kd,  "Duel");
        if (kla) dump_fields(kla, "LogArchive");
        if (klm) dump_fields(klm, "LogArchiveManager");
        nlog("model classes: Duel=%p LogArchive=%p LogArchiveManager=%p", kd, kla, klm);
    }
    int *pn = (int *)fld(self, "PlayerNum");
    nlog("PlayerNum=%d  savedExtraMode=%d", pn ? *pn : -1, g_selMode);

    struct { const char *name; int off; } objs[] = {
        { "CalculatorCanvas",         144 },
        { "CalculatorCanvasVertical", 152 },
        { "CalculatorMulti",          176 },
        { "CalculatorSingleVertical", 184 },
        { "CalculatorSingle",         192 },
        { "CurrentCalculator",        216 },
    };
    for (int i = 0; i < 6; i++) {
        void *go = *(void **)((char *)self + objs[i].off);
        nlog("--- %s = %p ---", objs[i].name, go);
        if (!go) continue;
        void *gtf = inv(m_go_get_transform, go, NULL);
        if (gtf) { dump_rect(gtf, objs[i].name); dump_tree(gtf, 0, 3, objs[i].name); }
    }

    /* the two duelist panels live under Duel/LifeArea */
    {
        void *multi0 = *(void **)((char *)self + 176);
        void *mtf0 = multi0 ? inv(m_go_get_transform, multi0, NULL) : NULL;
        void *duel = mtf0 ? tf_child(mtf0, 1) : NULL;          /* "Duel" */
        void *lifeArea = NULL;
        if (duel) {
            int dn = tf_children(duel);
            for (int i = 0; i < dn; i++) {
                void *c = tf_child(duel, i);
                char nm[64]; tf_name(c, nm, sizeof nm);
                if (!strcmp(nm, "LifeArea")) { lifeArea = c; break; }
            }
        }
        nlog("--- LifeArea = %p ---", lifeArea);
        if (lifeArea) {
            dump_rect(lifeArea, "LifeArea");
            int ln = tf_children(lifeArea);
            for (int i = 0; i < ln; i++) {
                void *c = tf_child(lifeArea, i);
                char nm[64]; tf_name(c, nm, sizeof nm);
                dump_rect(c, nm);
            }
            void *l1 = tf_child(lifeArea, 0);
            if (l1) {
                nlog("--- Life01 internals ---");
                dump_tree(l1, 0, 2, "Life01");
                int cn = tf_children(l1);
                for (int i = 0; i < cn; i++) {
                    void *c = tf_child(l1, i);
                    char nm[64]; tf_name(c, nm, sizeof nm);
                    dump_rect(c, nm);
                }
            }
        }
    }

    /* geometry of the two duelist panels inside CalculatorMulti */
    void *multi = *(void **)((char *)self + 176);
    void *mtf = multi ? inv(m_go_get_transform, multi, NULL) : NULL;
    if (mtf) {
        nlog("--- CalculatorMulti children geometry ---");
        int n = tf_children(mtf);
        for (int i = 0; i < n; i++) {
            void *c = tf_child(mtf, i);
            char nm[64]; tf_name(c, nm, sizeof nm);
            dump_rect(c, nm);
        }
    }
    {
        void *aui2 = il2cpp_domain_assembly_open(g_domain, "UnityEngine.UI");
        void *iui2 = aui2 ? il2cpp_assembly_get_image(aui2) : NULL;
        struct { void *img; const char *ns, *nm; } probe[] = {
            { g_img_cs,   "UISystem", "TweenScale" },
            { g_img_cs,   "UISystem", "TweenScaleFrom" },
            { g_img_cs,   "UISystem", "TweenScaleTo" },
            { g_img_cs,   "UISystem", "TweenPosition" },
            { g_img_cs,   "UISystem", "Tween" },
            { g_img_core, "UnityEngine", "Animator" },
            { g_img_core, "UnityEngine", "Animation" },
            { iui2,       "UnityEngine.UI", "LayoutElement" },
            { iui2,       "UnityEngine.UI", "ContentSizeFitter" },
            { iui2,       "UnityEngine.UI", "AspectRatioFitter" },
            { iui2,       "UnityEngine.UI", "HorizontalLayoutGroup" },
            { iui2,       "UnityEngine.UI", "VerticalLayoutGroup" },
            { iui2,       "UnityEngine.UI", "GridLayoutGroup" },
        };
        void *la2 = find_life_area(self);
        void *spots2[2] = { la2, la2 ? tf_child(la2, 0) : NULL };
        const char *spotname[2] = { "LifeArea", "Life01" };
        for (int t = 0; t < 2; t++) {
            if (!spots2[t]) continue;
            char line[512]; line[0] = 0;
            for (unsigned i = 0; i < sizeof probe / sizeof probe[0]; i++) {
                if (!probe[i].img) continue;
                void *k = il2cpp_class_from_name(probe[i].img, probe[i].ns, probe[i].nm);
                if (k && get_comp(spots2[t], k)) {
                    strncat(line, probe[i].nm, sizeof line - strlen(line) - 2);
                    strncat(line, " ", sizeof line - strlen(line) - 2);
                }
            }
            nlog("components on %s: %s", spotname[t], line[0] ? line : "(none of the probed)");
        }
    }
    if (g_selMode >= 3) build_four_player_layout(self);   /* both extra modes use the 4-panel grid */
    nlog("=== duel screen mapped ===");
}

static uint8_t (*orig_FixLayout_MoveNext)(void *, void *);

/* Calculator.FixLayout is a coroutine: it repaints the three stock radios a frame
   after OnEnable, which would leave both its row and ours lit.  Repaint after it. */
static uint8_t my_FixLayout_MoveNext(void *self, void *mi) {
    uint8_t r = orig_FixLayout_MoveNext ? orig_FixLayout_MoveNext(self, mi) : 0;
    if (g_selMode >= 3 && g_container && g_onSprite) apply_selection(g_container, g_selMode);
    return r;
}

/* find a class by bare name anywhere in an image (handles compiler-generated nested types) */
static void *find_class_by_name(void *image, const char *name) {
    if (!image || !il2cpp_image_get_class_count) return NULL;
    size_t n = il2cpp_image_get_class_count(image);
    for (size_t i = 0; i < n; i++) {
        void *k = il2cpp_image_get_class(image, i);
        if (!k) continue;
        const char *kn = il2cpp_class_get_name(k);
        if (kn && !strcmp(kn, name)) return k;
    }
    return NULL;
}

static void my_OnClickCalcMode(void *self, int number, void *mi) {
    nlog("OnClickCalcMode(%d)", number);
    /* The stock handler stores g_CalcMode = number and, for 3/4, paints no radio at
       all (verified in the disassembly), so letting it run is what makes the choice
       persist.  We only add the highlight for our own rows afterwards. */
    if (orig_OnClickCalcMode) orig_OnClickCalcMode(self, number, mi);
    save_mode(number >= 3 ? number : -1);
    apply_selection(g_container, number);
}
static int g_dumped = 0;

static void my_OnEnable(void *self, void *mi) {
    if (orig_OnEnable) orig_OnEnable(self, mi);
    if (g_dumped) return;
    g_dumped = 1;
    nlog("=== Calculator.OnEnable: adding modes ===");
    void *tf = inv(m_get_transform, self, NULL);
    void *node = tf;
    int path[6] = { 0, 0, 0, 0, 1, 0 };   /* ScrollView/Viewport/CalculatorSettings/Mode/Content/Toggle */
    for (int i = 0; i < 6 && node; i++) node = tf_child(node, path[i]);
    if (!node) { nlog("could not reach the mode toggle container"); return; }
    char nm[64]; tf_name(node, nm, sizeof nm);
    int n = tf_children(node);
    nlog("toggle container '%s' has %d rows", nm, n);
    if (n != 3 && n != 5) { nlog("unexpected row count %d, not touching it", n); return; }

    void *srcTf = tf_child(node, 0);                 /* the "Multi" row = "2 screens" */
    void *srcGo = srcTf ? inv(m_get_gameObject, srcTf, NULL) : NULL;
    if (!srcGo) { nlog("no source row"); return; }

    capture_sprites(self, node);
    g_container = node;

    if (n == 3) {
        clone_row(srcGo, node, "Four", "4 screens", 3);
        clone_row(srcGo, node, "Five", "5 screens", 4);
        nlog("container now has %d rows", tf_children(node));
    } else {
        nlog("extra rows already present, reusing them");
    }

    int cur = read_calc_mode();
    if (cur >= 3) g_selMode = cur;
    nlog("g_CalcMode=%d selMode=%d", cur, g_selMode);
    apply_selection(node, cur);
    nlog("=== done ===");
}


/* Waiting for the scripting runtime to be up.

   The mod is loaded from a <clinit>, long before Unity starts, so it has to
   wait.  It used to wait three seconds and then call il2cpp_domain_get().  That
   is a race, and it was lost the first time the phone was cold and off charge:
   the game took forty seconds to reach its title screen instead of fourteen,
   the call read a global the runtime had not written yet, and the process died
   with SIGSEGV before anything was drawn.

   There is no safe way to *ask* the runtime whether it is ready - every entry
   point has the same problem, and one of them answers by asserting, which is an
   abort rather than a fault and cannot be caught at all.  So do not ask: watch.
   IL2CPP starts its own garbage collector during initialisation, and the
   collector's finalizer thread shows up in /proc under a name of its own.  Once
   that thread exists the runtime is up, and reading /proc cannot crash anything.

   The fault handler stays as a second line of defence, and the probe is the
   whole chain rather than one call - domain, attach, assembly, image, a class we
   know exists - because il2cpp_domain_get() once came back with an
   uninitialised address that passed a null check and killed the process one
   call later. */
static sigjmp_buf g_probeJmp;
static volatile sig_atomic_t g_probing;

static void probe_fault(int sig) {
    if (g_probing) siglongjmp(g_probeJmp, 1);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* is there a thread by this name?  (comm is truncated to 15 characters) */
static int has_thread(const char *want) {
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    int found = 0;
    while (d && !found && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[128];
        snprintf(p, sizeof p, "/proc/self/task/%s/comm", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        char nm[64] = { 0 };
        if (fgets(nm, sizeof nm, f)) {
            char *nl = strchr(nm, '\n');
            if (nl) *nl = 0;
            if (!strcmp(nm, want)) found = 1;
        }
        fclose(f);
    }
    if (d) closedir(d);
    return found;
}

static void *probe_runtime(void **domainOut) {
    void *d = il2cpp_domain_get();
    if (!d) return NULL;
    il2cpp_thread_attach(d);
    void *asm1 = il2cpp_domain_assembly_open(d, "Assembly-CSharp");
    if (!asm1) return NULL;
    void *img = il2cpp_assembly_get_image(asm1);
    if (!img) return NULL;
    if (!il2cpp_class_from_name(img, "", "StartDuel")) return NULL;
    *domainOut = d;
    return img;
}

static void *wait_for_runtime(void **domainOut) {
    /* up to five minutes: a cold start behind a data download is slow */
    int waited = 0;
    while (!has_thread("GC Finalizer") && waited < 1500) { usleep(200000); waited++; }
    nlog("init: GC up after %.1fs", waited * 0.2);
    for (int i = 0; i < 300; i++) {
        struct sigaction sa, oldSegv, oldBus;
        void *img = NULL;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = probe_fault;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &oldSegv);
        sigaction(SIGBUS,  &sa, &oldBus);
        g_probing = 1;
        if (sigsetjmp(g_probeJmp, 1) == 0) img = probe_runtime(domainOut);
        g_probing = 0;
        sigaction(SIGSEGV, &oldSegv, NULL);
        sigaction(SIGBUS,  &oldBus,  NULL);
        if (img) {
            nlog("init: runtime up %.1fs after the GC", i * 0.2);
            return img;
        }
        usleep(200000);
    }
    return NULL;
}


/* The saved-log screen (Duel menu -> Log Archives -> a duel).

   Same two-column table as the in-duel dialog, built by DisplayLifeLog from the
   archive - and the archive itself only has room for two duelists, so there is
   nothing in it to widen.  First dump what it builds. */
/* Restarting a duel out of Log Archives.

   The game rebuilds the duel from an archive that only knows two duelists, so a
   five-player match came back as a two-player one on the current screen setting.
   The screen count of that match is saved with our own copy of its table, so
   put the setting back first and hand the restore the archive's id - the duel
   the game is about to build gets an id of its own. */
static void (*orig_RestartDuel)(void *, void *);
static void my_RestartDuel(void *self, void *mi) {
    void *arch = *(void **)((char *)self + 40);          /* Log */
    char id[80];
    archive_id(arch, id, sizeof id);
    if (logdb_load(id) && g_arcNp >= 3) {
        write_calc_mode(players_mode(g_arcNp));
        g_selMode = players_mode(g_arcNp);
        snprintf(g_restoreId, sizeof g_restoreId, "%s", id);
        nlog("restart: '%s' had %d players - screen mode set to %d", id, g_arcNp, g_selMode);
    }
    if (orig_RestartDuel) orig_RestartDuel(self, mi);
}

static void (*orig_DisplayLifeLog)(void *, void *);
static void my_DisplayLifeLog(void *self, void *mi) {
    if (orig_DisplayLifeLog) orig_DisplayLifeLog(self, mi);
    void *arch = *(void **)((char *)self + 40);          /* Log */
    char id[80];
    archive_id(arch, id, sizeof id);
    if (!logdb_load(id)) { nlog("archive '%s': no five-player table kept", id); return; }
    int nc = g_arcNp > 5 ? 5 : g_arcNp;
    void *obj = *(void **)((char *)self + 104);          /* LifeLogObj */
    void *tf  = obj ? inv(m_go_get_transform, obj, NULL) : NULL;
    if (!tf) return;
    void *h1 = *(void **)((char *)self + 128);           /* DuelistName1Text */
    void *h2 = *(void **)((char *)self + 136);
    void *t1 = h1 ? inv(m_get_transform, h1, NULL) : NULL;
    void *t2 = h2 ? inv(m_get_transform, h2, NULL) : NULL;
    {   /* one-shot: what the game wrote into the table before we touch it, so the
           rows can be lined up with our own entries */
        static int once;
        if (!once) {
            once = 1;
            int n = tf_children(tf);
            for (int i = 0; i < n; i++) {
                void *row = tf_child(tf, i);
                char rn[64]; tf_name(row, rn, sizeof rn);
                char line[240]; line[0] = 0;
                const char *grp[2] = { "Calculate", "Life" };
                for (int g = 0; g < 2; g++) {
                    void *gg = find_child(row, grp[g]);
                    int vis = gg ? tf_visible(gg) : -1;
                    char a[40] = "", b[40] = "";
                    if (gg) {
                        int m = tf_children(gg);
                        for (int j = 0; j < m; j++) {
                            void *ch = tf_child(gg, j);
                            char cn[64]; tf_name(ch, cn, sizeof cn);
                            if (!name_has(cn, "text")) continue;
                            void *c = get_comp(ch, k_TMP);
                            if (!c || !m_get_text) continue;
                            if (!strncmp(cn, "Duelist01", 9)) cs_str(inv(m_get_text, c, NULL), a, sizeof a);
                            if (!strncmp(cn, "Duelist02", 9)) cs_str(inv(m_get_text, c, NULL), b, sizeof b);
                        }
                    }
                    snprintf(line + strlen(line), sizeof line - strlen(line),
                             " %s(vis=%d '%s'|'%s')", grp[g], vis, a, b);
                }
                nlog("arcrow %d '%s' vis=%d%s", i, rn, tf_visible(row), line);
            }
        }
    }
    /* The header cell is the object the text sits in, not the text itself. */
    if (t1 && t2 && tf_parent(t1) != tf_parent(t2)) { t1 = tf_parent(t1); t2 = tf_parent(t2); }
    {   /* the table's own edges, in world units - the headings are laid out to
           match them rather than to their own parent, which is wider */
        void *rt = get_comp(tf, k_RectTransform);
        void *arr = (rt && k_Vector3 && il2cpp_array_new) ? il2cpp_array_new(k_Vector3, 4) : NULL;
        if (arr && t1 && t2) {
            void *a[1] = { arr };
            inv(m_GetWorldCorners, rt, a);
            float *f = (float *)((char *)arr + 32);
            float left = f[0], right = f[6];
            void *hp = tf_parent(t1);
            if (hp && k_HLG && m_set_enabled) {       /* stop the group re-placing them */
                void *lg = get_comp(hp, k_HLG);
                if (lg) { uint8_t off = 0; void *b[1] = { &off }; inv(m_set_enabled, lg, b); }
            }
            void *hdr[5] = { t1, t2, NULL, NULL, NULL };
            for (int c = 2; c < nc; c++) {
                char nm[32];
                snprintf(nm, sizeof nm, "ModName%d", c);
                void *have = hp ? find_child(hp, nm) : NULL;
                if (!have && hp) {
                    void *go = inv(m_get_gameObject, t1, NULL);
                    void *ar[1] = { go };
                    void *clone = inv(m_Instantiate, NULL, ar);
                    if (clone) {
                        void *ctf = inv(m_go_get_transform, clone, NULL);
                        uint8_t keep = 0;
                        void *sp[2] = { hp, &keep };
                        if (ctf) inv(m_SetParent, ctf, sp);
                        ar[0] = il2cpp_string_new(nm);
                        inv(m_set_name, clone, ar);
                        have = ctf;
                    }
                }
                hdr[c] = have;
            }
            for (int c = 0; c < nc; c++) {
                if (!hdr[c]) continue;
                place_world_column(hdr[c], c, nc, left, right);
                char t[NAMEMAX];
                if (g_arcNames[c][0]) snprintf(t, sizeof t, "%s", g_arcNames[c]);
                else snprintf(t, sizeof t, "Duelist %d", c + 1);
                set_tmp_tree(hdr[c], t, 3);
                cell_autosize(hdr[c]);
                int q = tf_children(hdr[c]);
                for (int j = 0; j < q; j++) cell_autosize(tf_child(hdr[c], j));
            }
        }
    }
    table_rows(tf, nc, g_arcStart, g_arcLife, g_arcN);
    nlog("archive '%s': %d columns, %d entries, %d rows", id, nc, g_arcN, tf_children(tf));
}

static void *worker(void *a) {
    (void)a;
    nlog("=== neuronmod loaded, pid=%d ===", getpid());
    for (int i = 0; i < 600 && !L; i++) {
        L = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
        if (!L) usleep(100000);
    }
    if (!L) { nlog("no libil2cpp"); return NULL; }
    g_base = module_base("libil2cpp.so");
    nlog("libil2cpp base = %p", (void *)g_base);

    SYM(il2cpp_domain_get); SYM(il2cpp_thread_attach); SYM(il2cpp_domain_assembly_open);
    SYM(il2cpp_assembly_get_image); SYM(il2cpp_class_from_name);
    SYM(il2cpp_class_get_method_from_name); SYM(il2cpp_class_get_fields);
    SYM(il2cpp_field_get_name); SYM(il2cpp_field_get_offset); SYM(il2cpp_field_get_type);
    SYM(il2cpp_type_get_name); SYM(il2cpp_object_get_class); SYM(il2cpp_class_get_name);
    SYM(il2cpp_runtime_invoke); SYM(il2cpp_object_unbox);
    SYM(il2cpp_string_chars); SYM(il2cpp_string_length);
    SYM(il2cpp_type_get_object); SYM(il2cpp_class_get_type); SYM(il2cpp_string_new);
    SYM(il2cpp_array_length); SYM(il2cpp_object_new); SYM(il2cpp_array_new);
    SYM(il2cpp_class_get_methods); SYM(il2cpp_method_get_name);
    SYM(il2cpp_method_get_param_count); SYM(il2cpp_method_get_param);
    SYM(il2cpp_method_is_generic);
    SYM(il2cpp_class_get_field_from_name); SYM(il2cpp_class_get_parent);
    SYM(il2cpp_field_static_get_value); SYM(il2cpp_field_static_set_value);
    SYM(il2cpp_image_get_class_count); SYM(il2cpp_image_get_class);

    logdb_wipe_if_asked();
    g_selMode = load_mode();
    load_names();
    nlog("persisted extra mode = %d", g_selMode);

    g_img_cs = wait_for_runtime(&g_domain);
    nlog("domain = %p, Assembly-CSharp image = %p", g_domain, g_img_cs);
    if (!g_domain || !g_img_cs) return NULL;
    unity_init();

    void *calc = cls(g_img_cs, "", "Calculator");
    void *mi = meth(calc, "OnEnable", 0);
    nlog("Calculator=%p OnEnable MethodInfo=%p", calc, mi);
    if (mi) {
        void **slot = (void **)mi;
        void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
        if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
            orig_OnEnable = (void (*)(void *, void *))slot[0];
            slot[0] = (void *)my_OnEnable;
            slot[1] = (void *)my_OnEnable;
            nlog("hooked Calculator.OnEnable (orig=%p)", (void *)orig_OnEnable);
        }
    }
    void *mclick = meth(calc, "OnClickCalcMode", 1);
    if (mclick) {
        void **slot = (void **)mclick;
        void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
        if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
            orig_OnClickCalcMode = (void (*)(void *, int, void *))slot[0];
            slot[0] = (void *)my_OnClickCalcMode;
            slot[1] = (void *)my_OnClickCalcMode;
            nlog("hooked Calculator.OnClickCalcMode (orig=%p)", (void *)orig_OnClickCalcMode);
        }
    }
    void *sd = cls(g_img_cs, "", "StartDuel");
    void *msd = sd ? meth(sd, "OnEnable", 0) : NULL;
    if (msd) {
        void **slot = (void **)msd;
        void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
        if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
            orig_StartDuel_OnEnable = (void (*)(void *, void *))slot[0];
            slot[0] = (void *)my_StartDuel_OnEnable;
            slot[1] = (void *)my_StartDuel_OnEnable;
            nlog("hooked StartDuel.OnEnable (orig=%p)", (void *)orig_StartDuel_OnEnable);
        }
    }
    {   /* Duel.CreatePlayers builds exactly two; we top the dictionary up after */
        void *kduel = cls(g_img_cs, "", "Duel");
        void *mcp = kduel ? meth(kduel, "CreatePlayers", 3) : NULL;
        if (mcp) {
            void **slot = (void **)mcp;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
                orig_CreatePlayers = (void (*)(void *, int, int, int, void *))slot[0];
                slot[0] = (void *)my_CreatePlayers;
                slot[1] = (void *)my_CreatePlayers;
                nlog("hooked Duel.CreatePlayers");
            }
        }
    }
    void *kfix = find_class_by_name(g_img_cs, "<FixLayout>d__23");
    void *mfix = kfix ? meth(kfix, "MoveNext", 0) : NULL;
    nlog("FixLayout coroutine class=%p MoveNext=%p", kfix, mfix);
    if (mfix) {
        void **slot = (void **)mfix;
        void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
        if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
            orig_FixLayout_MoveNext = (uint8_t (*)(void *, void *))slot[0];
            slot[0] = (void *)my_FixLayout_MoveNext;
            slot[1] = (void *)my_FixLayout_MoveNext;
            nlog("hooked FixLayout.MoveNext");
        }
    }
    if (sd) {
        struct { const char *name; void **orig; void *repl; } more[] = {
            { "Update",         (void **)&orig_StartDuel_Update,    (void *)my_StartDuel_Update },
            { "OnDisable",      (void **)&orig_StartDuel_OnDisable, (void *)my_StartDuel_OnDisable },
            { "OnClickDoReset", (void **)&orig_DoReset,             (void *)my_DoReset },
            { "OnClickUndo",    (void **)&orig_Undo,                (void *)my_Undo },
        };
        for (int i = 0; i < 4; i++) {
            void *m = meth(sd, more[i].name, 0);
            if (!m) continue;
            void **slot = (void **)m;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) != 0) continue;
            *more[i].orig = slot[0];
            slot[0] = more[i].repl;
            slot[1] = more[i].repl;
            nlog("hooked StartDuel.%s", more[i].name);
        }
        {   /* renaming: catch the submit, and the reset that clears names */
            struct { const char *n; int argc; void **orig; void *repl; } nh[] = {
                { "OnDuelistnameSubmit", 1, (void **)&orig_NameSubmit, (void *)my_NameSubmit },
                { "ResetDuelistName",    0, (void **)&orig_ResetNames, (void *)my_ResetNames },
            };
            for (int i = 0; i < 2; i++) {
                void *m = meth(sd, nh[i].n, nh[i].argc);
                if (!m) { nlog("no %s", nh[i].n); continue; }
                void **slot = (void **)m;
                void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
                if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) != 0) continue;
                *nh[i].orig = slot[0];
                slot[0] = nh[i].repl;
                slot[1] = nh[i].repl;
                nlog("hooked StartDuel.%s", nh[i].n);
            }
        }
        void *mcl = meth(sd, "OnClickLifePoint", 1);
        if (mcl) {
            void **slot = (void **)mcl;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
                orig_ClickLife = (void (*)(void *, int, void *))slot[0];
                slot[0] = (void *)my_ClickLife;
                slot[1] = (void *)my_ClickLife;
                nlog("hooked StartDuel.OnClickLifePoint");
            }
        }
    }
    {   /* one-shot: how does the game animate an LP number, and how does it
           store duelist names? */
        void *kt = cls(g_img_cs, "UISystem", "TweenCounterTmp");
        while (kt) {
            void *iter = NULL, *m;
            char line[900]; line[0] = 0;
            while ((m = il2cpp_class_get_methods(kt, &iter))) {
                char one[80];
                snprintf(one, sizeof one, "%s/%d ", il2cpp_method_get_name(m),
                         (int)il2cpp_method_get_param_count(m));
                if (strlen(line) + strlen(one) < sizeof line - 1) strcat(line, one);
            }
            nlog("methods of %s: %s", il2cpp_class_get_name(kt), line);
            kt = il2cpp_class_get_parent(kt);
        }
        void *ks = cls(g_img_cs, "", "CalculatorSettings");
        if (ks) dump_fields(ks, "CalculatorSettings");
    }
    {   /* the Log Archives list: readable row labels, and only Duel in the bar */
        void *kl = cls(g_img_cs, "", "LogArchiveList");
        struct { const char *name; void **orig; void *fn; } lh[] = {
            { "OnEnable",       (void **)&orig_LogList_OnEnable, (void *)my_LogList_OnEnable },
            { "DisplayLogList", (void **)&orig_DisplayLogList,   (void *)my_DisplayLogList },
        };
        for (unsigned li = 0; kl && li < sizeof lh / sizeof lh[0]; li++) {
            void *me = meth(kl, lh[li].name, 0);
            if (!me) continue;
            void **slot = (void **)me;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) != 0) continue;
            *lh[li].orig = slot[0];
            slot[0] = lh[li].fn;
            slot[1] = lh[li].fn;
            nlog("hooked LogArchiveList.%s", lh[li].name);
        }
        void *kf = cls(g_img_cs, "", "Footer");
        struct { const char *name; void **orig; void *fn; } fh[] = {
            { "OnEnable",       (void **)&orig_Footer_OnEnable, (void *)my_Footer_OnEnable },
            { "Start",          (void **)&orig_Footer_Start,    (void *)my_Footer_Start },
            { "DefalutMode",    (void **)&orig_Footer_Default,  (void *)my_Footer_Default },
            { "AdaptToContent", (void **)&orig_Footer_Adapt,    (void *)my_Footer_Adapt },
        };
        for (unsigned fi = 0; kf && fi < sizeof fh / sizeof fh[0]; fi++) {
            void *me = meth(kf, fh[fi].name, 0);
            if (!me) continue;
            void **slot = (void **)me;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) != 0) continue;
            *fh[fi].orig = slot[0];
            slot[0] = fh[fi].fn;
            slot[1] = fh[fi].fn;
            nlog("hooked Footer.%s", fh[fi].name);
        }
    }
    {   /* the saved-log screen: same two-column table, built from the archive */
        void *kd = cls(g_img_cs, "", "LogArchiveDetail");
        void *mr = kd ? meth(kd, "OnClickRestartDuel", 0) : NULL;
        if (mr) {
            void **slot = (void **)mr;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
                orig_RestartDuel = (void (*)(void *, void *))slot[0];
                slot[0] = (void *)my_RestartDuel;
                slot[1] = (void *)my_RestartDuel;
                nlog("hooked LogArchiveDetail.OnClickRestartDuel");
            }
        }
        void *m  = kd ? meth(kd, "OnEnable", 0) : NULL;
        if (m) {
            void **slot = (void **)m;
            void *page = (void *)((uintptr_t)slot & ~(uintptr_t)(getpagesize() - 1));
            if (mprotect(page, getpagesize() * 2, PROT_READ | PROT_WRITE) == 0) {
                orig_DisplayLifeLog = (void (*)(void *, void *))slot[0];
                slot[0] = (void *)my_DisplayLifeLog;
                slot[1] = (void *)my_DisplayLifeLog;
                nlog("hooked LogArchiveDetail.OnEnable");
            }
        }
    }
    {   /* one-shot: what can be set on a player, and what restarts a duel */
        void *kd = cls(g_img_cs, "", "Duel");
        void *kp = kd ? il2cpp_class_from_name(g_img_cs, "", "Duel/Player") : NULL;
        if (!kp) kp = il2cpp_class_from_name(g_img_cs, "Duel", "Player");
        if (kd) {
            void *iter = NULL, *m;
            char line[900]; line[0] = 0;
            while ((m = il2cpp_class_get_methods(kd, &iter))) {
                char one[80];
                snprintf(one, sizeof one, "%s/%d ", il2cpp_method_get_name(m),
                         (int)il2cpp_method_get_param_count(m));
                if (strlen(line) + strlen(one) < sizeof line - 1) strcat(line, one);
            }
            nlog("methods of Duel: %s", line);
        }
        if (kp) dump_fields(kp, "Duel.Player");
        void *kdet = cls(g_img_cs, "", "LogArchiveDetail");
        void *mr = kdet ? meth(kdet, "OnClickRestartDuel", 0) : NULL;
        nlog("restart entry = %p", mr);
    }
    nlog("=== neuronmod ready ===");
    return NULL;
}

/* listens for instruction text pushed from the dev machine and toasts it */
static void *instr_worker(void *a) {
    (void)a;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(INSTR_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        nlog("instruction socket bind failed");
        close(fd);
        return NULL;
    }
    nlog("instruction channel listening on 127.0.0.1:%d", INSTR_PORT);
    close(fd);
    /* UDP from another app does not reach us on this device, so poll a file
       that adb shell can write into our external files dir instead */
    const char *path = "/storage/emulated/0/Android/data/jp.konami.YugiohOcgSupports/files/neuronmod.say";
    struct stat st;
    time_t last_m = 0;
    off_t last_sz = -1;
    int announced = 0;
    for (;;) {
        if (stat(path, &st) == 0) {
            if (!announced) { nlog("instruction file found: %s", path); announced = 1; }
            if (st.st_mtime != last_m || st.st_size != last_sz) {
                last_m = st.st_mtime;
                last_sz = st.st_size;
                FILE *f = fopen(path, "r");
                if (f) {
                    char buf[512];
                    size_t n = fread(buf, 1, sizeof buf - 1, f);
                    fclose(f);
                    buf[n] = 0;
                    while (n && (buf[n - 1] == 0x0a || buf[n - 1] == 0x0d)) buf[--n] = 0;
                    if (n) { nlog("instruction: %s", buf); toast(buf); }
                }
            }
        }
        usleep(400000);
    }
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *r) {
    (void)r;
    g_vm = vm;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        jclass c = (*env)->FindClass(env, "neuron/mod/Toaster");
        if (c) {
            g_toaster = (jclass)(*env)->NewGlobalRef(env, c);
            g_show = (*env)->GetStaticMethodID(env, g_toaster, "show", "(Ljava/lang/String;Z)V");
        }
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    }
    pthread_t t; pthread_create(&t, NULL, worker, NULL); pthread_detach(t);
    pthread_t t2; pthread_create(&t2, NULL, instr_worker, NULL); pthread_detach(t2);
    return JNI_VERSION_1_6;
}
