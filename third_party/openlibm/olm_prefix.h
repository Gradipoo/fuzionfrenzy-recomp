/* Force-included into the openlibm sources we compile.
 * Renames the entry points the guest calls so Clang does not treat them as
 * builtins and the link does not bind them to libsystem. Whole tokens only:
 * sinf is not sin. C keywords and the IEEE primitives (sqrt, floor, fabs,
 * scalbn) stay host names; those are correctly rounded and are not renamed.
 */
#ifndef OLM_PREFIX_H
#define OLM_PREFIX_H
#define sin olm_sin
#define cos olm_cos
#define tan olm_tan
#define asin olm_asin
#define acos olm_acos
#define atan olm_atan
#define atan2 olm_atan2
#define log olm_log
#define log10 olm_log10
#define log2 olm_log2
#define exp olm_exp
#define exp2 olm_exp2
#define pow olm_pow
#define fmod olm_fmod
#define remainder olm_remainder
#define sinf olm_sinf
#define cosf olm_cosf
#define tanf olm_tanf
#define log10f olm_log10f
#define log2f olm_log2f
#define expf olm_expf
#define exp2f olm_exp2f
#define powf olm_powf
#define fmodf olm_fmodf
#define remainderf olm_remainderf
#define sincos olm_sincos
#define sincosf olm_sincosf
#endif
