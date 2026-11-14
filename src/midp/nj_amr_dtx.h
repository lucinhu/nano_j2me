/*
 * nano_j2me - AMR-NB SID reference vectors (numeric data only).
 * Apache-2.0 data from OpenCORE revision
 * 3b67218fb8efb776bcd79e7445774e02d778321d,
 * common/src/q_plsf_3_tbl.cpp#past_rq_init.
 * Copyright (C) 1998-2009 PacketVideo
 * (C) 2004, 3GPP Organizational Partners (ARIB, ATIS, CCSA, ETSI, TTA, TTC)
 * See docs/licenses/AMR-Apache-2.0.txt and
 * docs/licenses/AMR-OpenCORE-NOTICE.txt for license and attribution.
 * Modification: reshape 80 signed Q15 frequency residuals into [8][10];
 * values are unchanged. Runtime algorithms are independently implemented.
 */
#ifndef NJ_AMR_DTX_H
#define NJ_AMR_DTX_H
static const nj_s2 nj_amr_sid_reference[8][10] = {
    {-258,-318,-439,-634,-656,-773,-711,-502,-268,-193},
    {-2,125,122,-39,-9,105,129,283,372,575},
    {-277,-324,-197,-487,-445,-362,-292,-27,177,543},
    {342,517,516,130,27,-104,-120,-140,-74,-56},
    {-564,-943,-1520,-965,-814,-526,-322,-2,159,657},
    {-312,-284,-386,-597,-493,-526,-418,-229,105,449},
    {-557,-870,-1075,-919,-950,-752,-709,-316,62,486},
    {-314,-191,-203,-330,-160,-103,-51,131,338,515}
};
#endif
