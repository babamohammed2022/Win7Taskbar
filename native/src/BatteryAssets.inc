// Win7Taskbar - battery icons cut from the strip supplied in
// assets/icon-sources/ (never committed: see .gitignore).
// Cut measured on the alpha bounding box of every glyph: no icon is invented
// and no size is hard-coded by hand.
//
// DO NOT EDIT BY HAND: re-run
//   python3 "compilation files/icons_to_base64.py" --style battery ...
//
// Order: 0-9 green (10..100%%), 10-19 yellow (10..100%%), 20-25 red (10..60%%),
// 26 empty/grey, 27 battery with X (missing), 28 warning triangle,
// 29 circle with X (charge error), 30 plug (AC).

#pragma once

namespace w7t {
namespace battassets {

struct IconB64 { const char* b64; int nativeW; int nativeH; };

/*
 * Mapping of the source strip (Bitmap303.png, 42 drawings) to the table.
 * The strip carries three designs: battery + white fill (on battery), the
 * same battery with the AC plug (charging) and a set of narrow bars. Only
 * the drawings the callers need are used, one per state:
 *     0..8   on-battery levels 1..9   (strip glyphs 0..8, fill grows 1..9 rows)
 *     9      level 10: the strip has nine fill steps, the tallest is reused
 *    10..18  on-AC levels 1..9        (strip glyphs 32..40, plug + fill)
 *    19      on-AC level 10: tallest of that series, same reason
 *    20..25  low levels 1..6          (strip glyphs 0..5)
 *    26      grey battery, no fill     (strip glyph 29)
 *    27      on AC with no battery     (strip glyph 31: plug, no fill)
 *    28      warning triangle          (strip glyph 18)
 *    29      plug + red X              (strip glyph 30: charging refused)
 * Regenerate with the --cells list above. The strip also contains the
 * on-battery series twice and a bar-only series: both unused on purpose.
 */

/* kLevel1 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel1[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAaElEQVR42u2SwQ2AMAhFH4aNTKfoUK0rmXSKxpnwVFPRGr37TsD/cACEMxEI
XV6B0hLxJjNLhyiy+KYIZHsAyEBUIJhZ6qZcaLq6+nbjnVsw8YHf3KNu+as39MfSkTDi9W/I4DU9FSg7oGZTAihACSwAAAAASUVORK5CYII=
)W7B";

/* kLevel2 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel2[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbElEQVR42t2Suw2AMAwFzygboUyRoRJWQsoUiJlMFWQMREDJVf48u7CfcCQB
0eQLUFsiXqSqeW+KTH4oAUU7AAVIAYiqms2WE60fXH290I4tGHjB78XB3XP2Anv/75t7X7ROe+QNubGmZwHqBl3GUwLhRNS4AAAAAElFTkSu
QmCC
)W7B";

/* kLevel3 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel3[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbUlEQVR42tWSwQ2AIAxFXw0bGaZgKHAlE6YwzlRPmFqV6MXEd6L8T9OULxxJ
QDT1AtRWiDepat5Fkck/SkDRDkABUgCiqmbT5UTTg7tfL7xjOwy84I/m4FY0e4Nd6Udj9H7RJu1RNuQmmp4FqBsbJlMCv+mFzwAAAABJRU5E
rkJggg==
)W7B";

/* kLevel4 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel4[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbElEQVR42s2SwQ2AIAxFXw0bGaZgKHAlE6YgzlRPmFqV6M13aumnKeULZxIQ
Td6A2hPxIlXNR1Fk8ZcSUHQAUIAUgKiq2XS50OvBnW832rkHEx/4iTi4V69eYLf0x5lHv2id9sob8mBNTwPqDth3UwJ5VadjAAAAAElFTkSu
QmCC
)W7B";

/* kLevel5 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel5[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAb0lEQVR42s2S0Q2AIAxEXw0bGaZgKHAlE6YgzlS/MFiVqImJ74u2B+HSE/YE
wDd1AXItxIpUNW5DkcleCkDSDkACggO8qsbmlQN17kx/OdGO9TDwgO/EzhiZraA1/pM/vzfY22KbtFvZkItoWgqQV5XXUwJ9B/G1AAAAAElF
TkSuQmCC
)W7B";

/* kLevel6 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel6[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbUlEQVR42s2SwQ2AIAwAr4aNDFMwFLiSCVMQZ6ovDFYx8jDxXi29kjapcCYA
vskLkGsiVlLVeBRFFtsUgKQPAAkIDvCqGptfLtS6M+/bjTvXYGKAIdmZ2VYrtLt8N8ZPZNfbvMfr25DOaVoKkHdTN1MCfDWNzwAAAABJRU5E
rkJggg==
)W7B";

/* kLevel7 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel7[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAa0lEQVR42t2S2wmAMAxFTyQbSafoUK0rCZ2iOFP8qtRoRX89X3ncPAgRzkQg
dH4FSnPEi8wsHUmRxRdFINsDQAaiAsHMUtflQsuri2832rkZEx9QN271gn69T51/L9bRmUa8/g0ZvKanAmUHEJdTAtZLzkwAAAAASUVORK5C
YII=
)W7B";

/* kLevel8 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel8[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAaUlEQVR42u2S2wmAMAxFTyQbSafoUK0rCZ2iOFP8qtRoRf89X3nc3ECIcCYC
ocsrUFoiXmRm6WiKLH4oAtkeADIQFQhmljqXC62vrr7daOcWqHNYvbLfOPGBX9yjo5uOeP0bMnhNTwXKDs3oUwJ9E2V9AAAAAElFTkSuQmCC
)W7B";

/* kLevel9 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel9[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAXklEQVR42u2SwRGAIAwENw4tUQVFgUVRBUWdLxyIg6O+3d/lLvdJjJkExEE3
oHZhPiQpn6bZ7pcSUHQDUIAUgCgpDy0Xuh/80AfHko0X/OHP4bA6wIrHv2GL1/Q0oB4WilHNd3kI3wAAAABJRU5ErkJggg==
)W7B";

/* kLevel10 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLevel10[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAXklEQVR42u2SwRGAIAwENw4tUQVFgUVRBUWdLxyIg6O+3d/lLvdJjJkExEE3
oHZhPiQpn6bZ7pcSUHQDUIAUgCgpDy0Xuh/80AfHko0X/OHP4bA6wIrHv2GL1/Q0oB4WilHNd3kI3wAAAABJRU5ErkJggg==
)W7B";

/* kCharging1 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging1[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABDklEQVR42qWSsUrEQBCGv5E8QUqt7YRUV6+95G3SWO3uwyipj1x/KeVsAlec
HIIvsEiMcKDF2mSPdc3lCn9Ydph/fv6ZYYTfuAMW/MVm/BdjvEpFBjA+ArAe/4fAjXUAZMHNe69TO6WUGsOriW7IglBEbJS/jgRKKaXatm0j
3oRR4nbK8T36MwBMxjT2IrLVWt9Mkdbads551j0s7YJ/YE58cM4d0mTf9wPwcU68r6rqyTn3FRLDMHwaY5bA83HtJ2YugXvgTWvttdYeWAMq
dpgTl+HK0usKR3KE936Z9i4i7am5sqTQTtTczok3ImKnbhugrmu6rnvJ8/wd2KXi1YwrwGXTNN9FUWyB15j4AQ8zweKLYoLmAAAAAElFTkSu
QmCC
)W7B";

/* kCharging2 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging2[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABBklEQVR42qWSsWrDMBCGPxU/gcd07lbwlFnZS97GSyfJD9PiuTi7PZZ0MWRo
CYW+gCiuC4F2UBcpKKojF/qDuONOP//dLwlOcQMs+Y2ti0uXb2KSBrQNALQu3vmeuwdA5tWstSqWk1JKl15OTEPmiUKIKqhfBQQppZRd13VB
X/tVwnHW7tzbGQA6Yxp7IcROKXU91ayqqkspJ9W9aRf8AynywRhziIvDMIzAxxx5X5blozHmyxfGcfzUWj8AT0fbz+y8Bm6BN6WUVUpZoAVk
qHAknzGnjY36y86zOHnn6Jd5rFLkrRCimvrbAHVd0/f9S57n78BzTN4kVAEWTdN8F0WxA17Dxg92dM/YGW+SAAAAAABJRU5ErkJggg==
)W7B";

/* kCharging3 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging3[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABEElEQVR42qWSsUoEMRRFT2S+YJhKCys7YaqtYy/+zfTJfIe1MvUy22/sZLUY
2EJZLPyAIOMIC1rEJlmycZwVvBDy8l4u972bCPZxCcz4iZXfZz5epCQNaBcBWPr9JtT8PQCyoOacU6mclFL68GSkG7JAFELUUf4sIkgppTTG
mKiuwyhxO1d+3boDAHTGODZCiLVS6nysWNe1mVKeVA+mHfEPTJG31tptmuz7fgDeD5E3VVXdW2s/Q2IYhg+t9Rx4CE+1g3NuHp+NMRRF8aqU
OvVGPQLXwN3uzYIJv5izTI36y8wHsdd28ssCLqbIKyFEPfa3AZqmoeu65zzP34CnlLyYUAU4btv2qyzLNfASF74Bp6zOtJb3HsEAAAAASUVO
RK5CYII=
)W7B";

/* kCharging4 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging4[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABD0lEQVR42qWSsUrEQBCGv5XgAySd1nZCqqvXNojPtLsPo6Q+cqRN0p5N4ApF
BF8gSIxwoMXa7B57a4zF/bDMMLM//8zwC45xC6z4ja2LK5dvYpIGtA0ANC7e+577B0Di1ay1KpaTUkqXXs5MQ+KJQggT1K8CgpRSyrZt26Cv
/SrhOHfuPdh/AOiEebwIIXZKqeu5pjGmXVJeVPdHO+MELJH3wzDs4+I4jhPw4a99gLV27fOu66jrmqIovrIsOweYpulTa70GHg9n93vEuzVN
Y4E3pZRVSlmgAWQo+Cc5dFnsrpMPdrRz5DKPmyXyVghh5rwNUJYlfd8/p2n6DjzF5M2CKsBFVVXfeZ7vgNew8QObDuTJwHyUAAAAAABJRU5E
rkJggg==
)W7B";

/* kCharging5 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging5[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABC0lEQVR42q2SMU7EMBBFn1HqFCmhpkNKk61Nv+JMdi6QW4BSW9k+qZcm0hYg
hMQFIpSECgrT2MgxIRTwJWtG8/31Z0YjWGIP7PiOo4s7lx9ikQa0DQC0Lt56zv0DIPFu1loV20kppUsvVroh8UIhRBnULwOBlFLKruu6gNd+
lLCdG/fu7C8AdMI6noQQJ6XU1RpZlmW35bzp7pd2xh+waNtaa3xeVRXTNJGm6UIwjuMMTJvORVFgjGEYhndfm+f5TWttgPuvtfs54tnatrXA
i1LKKqUs0AIyNPlRHF5ZfF3/u7Doyjyut8RHIUS5dtsAdV3T9/1jlmWvwEMsPmy4Apw3TfOR5/kJeA6JT0+s5LL6FN/DAAAAAElFTkSuQmCC
)W7B";

/* kCharging6 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging6[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABDUlEQVR42o2SMWvDMBCFPxXPHjw2c7eCF2dW95LfJPnHtHg2zm7P6WLI0FIK
/QOm2O7UDuoiBVlxlD4Qd9zT490dJ1jiEdhyjoONW5vvQ5EGtPEAtDY+Oc7+AyBxbsYYFdpJKaVNNyvdkDihEKL06neeQEopZdd1ncdrN4rf
zs6+Z3MFgE5Yx7sQ4qiUul8jy7LsYs5Rd7e0hbMxpj4NpTXXcHOJSNOUaZrO6uM4zsAUFRdFQV3XDMPw42rzPH9rrWvg5dShmyOcrW1bA3wq
pYxSygAtIH2Ti2L/ysLrirb9Hyy2HVyZw0NMfBBClGu3DVBVFX3fv2VZ9gW8huJ9xBXgtmma3zzPj8CHT/wBQezkiBsdTbQAAAAASUVORK5C
YII=
)W7B";

/* kCharging7 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging7[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABCElEQVR42qWSsU6EQBCGvzXUFJRa25nQcPXaG59plofRUBOuh/psLrnijDHx
BYgBrLQYG9bsAYfF/clmJjP77z8zO4ZTPAAb5tiNdjP62ynJAU4DAPVon3xuvAdA5NVUVaZy1lo7ujcL1RB5ojEmD+K3AcFaa23TNE2Qd76V
sJzH8TzrPwBcxDLejDEHEblbSuZ53syUw5dFZFUV4IoLcJYcxzF938/iXdcNQL9KzrKMsixp2/bbx4Zh+HLOlcDL39iXelZVretagQ8RURFR
oAZsKHKWHG7ZdLsuHtjJP0+2zON+jbwzxuRLuw1QFAX7/f41SZJP4Dglb1dUAa6rqvpJ0/QAvIeJXzZM7DourxBfAAAAAElFTkSuQmCC
)W7B";

/* kCharging8 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging8[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABC0lEQVR42qWSsW6EMAyGv1TMDIzt3JmFm5O96jMlPEwrZgQ7zNcFqcN1qNQX
QBXQqR3cJVQ54OhwvxTZsvPntx0rzvEAHFjj6O3B+/WS5AAnAYDG26c55+8BEM1qImKXclpr7d27jWqIZqJSKg/i9wFBa61127ZtkHdzK2E5
j/48yz8AXBSWISIlQF3XGGMwxmxVS57n7Uo5fNlau6sKcMMVuEiO45hxHFfxYRgmYNwlZ1lGWZb0ff89x6Zp+nLOlcDL39i3ehYRaZpGgA9r
rVhrBWgAHYpcJIdbttyuqwd29s+LLZth9shHpVS+tdsARVHQdd1bkiSfwGlJrndUAW6rqvpJ0/QVeA8Tv2Fb8C1ntFZlAAAAAElFTkSuQmCC
)W7B";

/* kCharging9 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging9[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABBElEQVR42qWSIW+EQBCFv23QCGSrqzGgd31/1C4/pgmacB701ZBUtOKS/gHS
AFWtmBr2sscBJ+6Zmczsy3szO4pLvAA51zjOMZ/zw5LkACcBgGaOr743vwMg8moiYpdyWms9p08rbog8USlVBPXngKC11rpt2zboOz/K2Y63
a62VWwBctGYnz3OMMRhj1toURdFuKt9S90t74A5skuM4ZhzHq/owDBMw7pKzLKOqKvq+//W1aZp+nHMV8HZe+9rMIiJN0wjwZa0Va60ADaBD
kU1yeGXL67p7YRf/vLgyD7NHPiqlirXbBijLkq7rPpMk+QY+luTDjirAY13Xf2mavgOnsPEP0Mb0MrSC8IIAAAAASUVORK5CYII=
)W7B";

/* kCharging10 (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kCharging10[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABBElEQVR42qWSIW+EQBCFv23QCGSrqzGgd31/1C4/pgmacB701ZBUtOKS/gHS
AFWtmBr2sscBJ+6Zmczsy3szO4pLvAA51zjOMZ/zw5LkACcBgGaOr743vwMg8moiYpdyWms9p08rbog8USlVBPXngKC11rpt2zboOz/K2Y63
a62VWwBctGYnz3OMMRhj1toURdFuKt9S90t74A5skuM4ZhzHq/owDBMw7pKzLKOqKvq+//W1aZp+nHMV8HZe+9rMIiJN0wjwZa0Va60ADaBD
kU1yeGXL67p7YRf/vLgyD7NHPiqlirXbBijLkq7rPpMk+QY+luTDjirAY13Xf2mavgOnsPEP0Mb0MrSC8IIAAAAASUVORK5CYII=
)W7B";

/* kLow1 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLow1[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAaElEQVR42u2SwQ2AMAhFH4aNTKfoUK0rmXSKxpnwVFPRGr37TsD/cACEMxEI
XV6B0hLxJjNLhyiy+KYIZHsAyEBUIJhZ6qZcaLq6+nbjnVsw8YHf3KNu+as39MfSkTDi9W/I4DU9FSg7oGZTAihACSwAAAAASUVORK5CYII=
)W7B";

/* kLow2 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLow2[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbElEQVR42t2Suw2AMAwFzygboUyRoRJWQsoUiJlMFWQMREDJVf48u7CfcCQB
0eQLUFsiXqSqeW+KTH4oAUU7AAVIAYiqms2WE60fXH290I4tGHjB78XB3XP2Anv/75t7X7ROe+QNubGmZwHqBl3GUwLhRNS4AAAAAElFTkSu
QmCC
)W7B";

/* kLow3 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLow3[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbUlEQVR42tWSwQ2AIAxFXw0bGaZgKHAlE6YwzlRPmFqV6MXEd6L8T9OULxxJ
QDT1AtRWiDepat5Fkck/SkDRDkABUgCiqmbT5UTTg7tfL7xjOwy84I/m4FY0e4Nd6Udj9H7RJu1RNuQmmp4FqBsbJlMCv+mFzwAAAABJRU5E
rkJggg==
)W7B";

/* kLow4 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLow4[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbElEQVR42s2SwQ2AIAxFXw0bGaZgKHAlE6YgzlRPmFqV6M13aumnKeULZxIQ
Td6A2hPxIlXNR1Fk8ZcSUHQAUIAUgKiq2XS50OvBnW832rkHEx/4iTi4V69eYLf0x5lHv2id9sob8mBNTwPqDth3UwJ5VadjAAAAAElFTkSu
QmCC
)W7B";

/* kLow5 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLow5[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAb0lEQVR42s2S0Q2AIAxEXw0bGaZgKHAlE6YgzlS/MFiVqImJ74u2B+HSE/YE
wDd1AXItxIpUNW5DkcleCkDSDkACggO8qsbmlQN17kx/OdGO9TDwgO/EzhiZraA1/pM/vzfY22KbtFvZkItoWgqQV5XXUwJ9B/G1AAAAAElF
TkSuQmCC
)W7B";

/* kLow6 (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kLow6[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAbUlEQVR42s2SwQ2AIAwAr4aNDFMwFLiSCVMQZ6ovDFYx8jDxXi29kjapcCYA
vskLkGsiVlLVeBRFFtsUgKQPAAkIDvCqGptfLtS6M+/bjTvXYGKAIdmZ2VYrtLt8N8ZPZNfbvMfr25DOaVoKkHdTN1MCfDWNzwAAAABJRU5E
rkJggg==
)W7B";

/* kEmpty (11x16) <- assets/icon-sources/Bitmap303.png */
static const char kEmpty[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAsAAAAQCAYAAADAvYV+AAAAYElEQVR42mNkQAXeDAwMpkj80wwMDFthHEZ0Rdu2bauHSXp5eTUia2KEKURW
hA6gmk6zwBQimYIBYPIsaOJnsag1hjGYGEgAo4rJVsyCKwKwAZLSBiOOpIkOTjMwMGwFAP2IHiAwO8shAAAAAElFTkSuQmCC
)W7B";

/* kNoBatt (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kNoBatt[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABL0lEQVR42qWTv07DMBCHv0N5go4wIAaYyIQ6Z3fE23S3/RwsmfoAKGTGbKjt
0D9IoC68QIRKkCrBYBZHMiHQIn6S5dOdP/vudBa+KgeGfNck7MNg3wAkXaiqKt0SSinnvc9EZAysvfdaRGwM58AwhlqlaZoF86gnG5IWVErZ
yH8aAVmaptlyuXRR3ACTpHPZLOxn3vv2VRaLBUAG4L3XACJiE/q1FpGV1vq8L2itdW0TTVVVPqRyGa2x/0HhLAf8Q7/B27qut13nZrNpgNdd
8Ho0Gt3Xdf3eOpqmeTPGXAPTeEj69FAUhRRFcaK1Pg6NmgFXwN0+Na+AZ2MMxhiAW8DtU/NOddO++Cs8UUrZvtkGcM4xn8+fBoPBC/AYx2TH
VwQ4LMvyI8/zlYhMo+/JJ1kciX/ZR+xYAAAAAElFTkSuQmCC
)W7B";

/* kWarn (10x9) <- assets/icon-sources/Bitmap303.png */
static const char kWarn[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAAoAAAAJCAYAAAALpr0TAAABCUlEQVR42o2Mv0rDUBhHf99NRIm1SnNRcFQoGP/SoW6CtTr5AiJS8DVcVYqC
KC6+gNzOLvoKyeroLkqITbVyb9F+n5OS0bOewyEUiE24Q0S3ACAiB+t72cOvU8WQiK6XNpb12nZNE9FV0anC7dg6rj5nCo9PDOu4GpuwXYyR
mNBLOjr9yppyfrIoW40ZGXabknR0GpvQ/zsKcGoda79ShvA3ZMhQkwGsYw2gDQAqNqEiosOoPg/O3zFV8jA7PQLOPxDV50BErdiEygdwYR3r
gCz4tYfdzQCNWgn8lmLc82Ad67FRulREtL8alaH6GfxBD+auj6OzF/iDHN5nFysLE3BOWnR/UxH8gx/iV2Zk8ZiPqgAAAABJRU5ErkJggg==
)W7B";

/* kChargeErr (15x16) <- assets/icon-sources/Bitmap303.png */
static const char kChargeErr[] = R"W7B(
iVBORw0KGgoAAAANSUhEUgAAAA8AAAAQCAYAAADJViUEAAABsElEQVR42oWTwWoTURSGv9vOajoJIYJaKrrSlbPMRtS4EpzBvTIvkU3WN/Mu
2gco8YLgIhFEoQpp0oVSWu2giQgKSUVcCL8LZ8IkTfCH4R7ume+ec/6Za1hUDDQ4r/18beTxMwBvGXLO2YKIoqgvqWmM2QWOJFljTFqGY6BR
hgqFYdjMw50V3eAVYBRFaWn/eglohmHYHI1G/VK+A+x7S4e9y9cbkoqqDIdDgCaAJAtgjEk9VuvIGHNorb25Kpmmab8wseOcU97Kw9KzqzXK
32UD4EEU8bx+4Zxho0vbYAzrtAGQ3brN/R/fGW9V9orEq6vXHoffvpLV6gvAdDo9A2Zz55Ik0XirIoGOg6pe7lyRQB+Ditrt9p+i3dlsdtZq
tZ4Ad+dwPnPvpFLVzPP0e3NTx0FVwAvgk7VW1loBvcL1ZbhTVBRo7AfKjestG7Uwcz6jvfPlMydBlYkfsP3rJxM/2OM/6hxcvDyfMW+tN/ED
ffB9ndbqWlcZIBbotFZXkiRyzsk5pyRJ/oGgwWDwPsuy18CjMlh8xKf5v9xbOvgeUOl2u2/iOD40xrwtXU/+ApUL6ZszYckdAAAAAElFTkSu
QmCC
)W7B";

/* Table: index -> glyph. */
static const IconB64 kAll[] = {
    { kLevel1, 11, 16 },
    { kLevel2, 11, 16 },
    { kLevel3, 11, 16 },
    { kLevel4, 11, 16 },
    { kLevel5, 11, 16 },
    { kLevel6, 11, 16 },
    { kLevel7, 11, 16 },
    { kLevel8, 11, 16 },
    { kLevel9, 11, 16 },
    { kLevel10, 11, 16 },
    { kCharging1, 15, 16 },
    { kCharging2, 15, 16 },
    { kCharging3, 15, 16 },
    { kCharging4, 15, 16 },
    { kCharging5, 15, 16 },
    { kCharging6, 15, 16 },
    { kCharging7, 15, 16 },
    { kCharging8, 15, 16 },
    { kCharging9, 15, 16 },
    { kCharging10, 15, 16 },
    { kLow1, 11, 16 },
    { kLow2, 11, 16 },
    { kLow3, 11, 16 },
    { kLow4, 11, 16 },
    { kLow5, 11, 16 },
    { kLow6, 11, 16 },
    { kEmpty, 11, 16 },
    { kNoBatt, 15, 16 },
    { kWarn, 10, 9 },
    { kChargeErr, 15, 16 },
};

/* Idx* prefix so that it does not clash with the base64 array names. The
 * names say what the drawing shows, because that is what the callers pick:
 * the strip has no colour variants (the fill is white), so "low" and "full"
 * differ by the height of the fill, not by a colour. */
enum Idx {
    IdxLevelBase = 0,    /* on battery: + level 1..10 -> base + lvl - 1 */
    IdxChargingBase = 10,/* on AC, battery + plug: same level arithmetic */
    IdxLowBase = 20,     /* low/critical levels 1..6 */
    IdxEmpty = 26,       /* battery outline, no fill (unknown/absent) */
    IdxNoBatt = 27,      /* on AC with no battery at all */
    IdxWarn = 28,        /* warning triangle */
    IdxChargeErr = 29,   /* AC plug + red X: charging refuses */
    IdxCount = 30,
};

} // namespace battassets
} // namespace w7t
