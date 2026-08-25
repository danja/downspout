#pragma once
/* Audio sample descriptor extraction — C API.
 * Spec: docs/campione-profiles/01-descriptor-spec.md
 * All accumulation is double (IEEE 754 binary64). */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SP_VECTOR_SIZE   65
#define SP_BAND_COUNT     8
#define SP_ENV_SEG_COUNT 10
#define SP_STAT_COUNT     9  /* number of per-frame spectral descriptors */

typedef struct sp_params_t {
    int fft_size;  /* 0 = default 1024 */
    int hop_size;  /* 0 = default 256  */
} sp_params_t;

typedef struct sp_frame_stat_t {
    double mean;
    double std_dev;
    bool   defined;
} sp_frame_stat_t;

typedef struct sp_profile_t {
    /* §3 Container metadata */
    double sample_rate;
    int    channel_count;
    size_t frame_count;

    /* §4.1 Temporal */
    double duration_seconds;
    double effective_duration_seconds;
    double attack_time_seconds;
    double log_attack_time;
    double temporal_centroid_normalised;
    double crest_factor_db;
    double decay_slope_db_per_second;
    double decay_fit_r2;

    /* §4.2 Levels */
    double peak_dbfs;
    double rms_dbfs;

    /* §4.5 Frame summaries — order matches §4.4 table:
       [0] centroid  [1] spread    [2] skewness  [3] kurtosis
       [4] rolloff85 [5] rolloff95 [6] flatness  [7] flux  [8] zcr */
    sp_frame_stat_t frame_stat[SP_STAT_COUNT];

    /* §4.6 Attack / tail / dominant partial */
    double attack_centroid;
    double tail_centroid;
    double attack_tail_centroid_delta;
    double attack_flatness;
    double tail_flatness;
    double dominant_partial_frequency;
    double dominant_partial_salience;

    /* §4.7 Band energies (shares, sum to 1.0) */
    double band_energy[SP_BAND_COUNT];

    /* §4.8 Envelope segments (§4.8) */
    double env_rms_db[SP_ENV_SEG_COUNT];
    double env_high_band_rms_db[SP_ENV_SEG_COUNT];

    /* §4.9 Stereo */
    double inter_channel_correlation;
    double mid_side_ratio_db;

    /* §4.10 Advisory flags */
    bool silent;
    bool clipped;
    bool multiple_onsets;
    bool sustained;
    bool phase_warning;

    /* Canonical 65-element vector; undefined elements are NaN */
    double vector[SP_VECTOR_SIZE];
    bool   valid[SP_VECTOR_SIZE];
} sp_profile_t;

/* Main entry point.
 * channels: array of channel_count pointers, each frame_count floats in [-1,1].
 * params:   NULL for defaults (fft_size=1024, hop_size=256). */
sp_profile_t sp_analyse(
    const float* const* channels,
    int                 channel_count,
    size_t              frame_count,
    double              sample_rate,
    const sp_params_t*  params
);

#ifdef __cplusplus
}
#endif
