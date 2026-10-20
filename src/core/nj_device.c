/*
 * nano_j2me - nj_device.c
 *
 * The built-in handset profiles.  See include/nj2me/nj_device.h for why the
 * runtime carries them at all.
 *
 * ---------------------------------------------------------------------------
 * HOW THESE WERE CHOSEN
 * ---------------------------------------------------------------------------
 * The keys are the ones the collected corpus actually asks for - the list is a
 * count of what the 395 titles in tests/games probe, not a supplier's
 * datasheet.  The values are the shape a real handset answers with:
 *
 *   microedition.platform   "Nokia6230/05.50" - model and firmware, which is
 *                           what Series 40 returns and what a title's
 *                           startsWith("Nokia") test sees.
 *   com.*.imei / IMEI       15 digits with a valid Luhn check digit.  The TAC
 *                           is synthetic: 357000 is not allocated to anybody.
 *   com.*.imsi / IMSI       15 digits: MCC 310, MNC 15, then ten zeroes.
 *                           MCC 310 is the United States, and MNC 15 is not
 *                           assigned, so this is nobody's subscription.
 *   com.nokia.mid.networkID "31015" - the MCC/MNC pair as one string, which is
 *                           the spelling Series 40 uses.
 *
 * Everything here is deliberately the same across vendors where a title would
 * not look at it: there is no reason for four fake handsets to pretend to have
 * four different SIMs, and one identity is easier to recognise in a log.
 *
 * A profile is not a promise that the runtime IMPLEMENTS the handset - the
 * Nokia UI classes are compiled in for every build regardless (see
 * src/midp/nj_lib_nokia.c), and no profile turns a feature on or off.  It only
 * changes what System.getProperty() answers.
 */
#include "../core/nj_internal.h"

#include "nj2me/nj_device.h"

/* Count of a table defined in this file. */
#define NJ_DEVICE_N(a) ((nj_u4)(sizeof(a) / sizeof((a)[0])))

/* One fake subscriber, four handsets.  See the file comment. */
#define NJ_ID_IMEI "357000000000009"
#define NJ_ID_IMSI "310150000000000"
#define NJ_ID_MCC  "310"
#define NJ_ID_MNC  "15"
#define NJ_ID_NET  "31015"

/*
 * Nokia 6230: Series 40, MIDP 2.0, and the handset most of the corpus's Nokia
 * branches were written against.  It answers the identity under two spellings
 * because games in the wild use both - `com.nokia.mid.imei` on newer Series 40
 * firmware and `com.nokia.IMEI` on the older builds.
 *
 * `com.nokia.keyboard.type` says PhoneKeypad here, where the desktop ports
 * answer None: a title that hid its on-screen keypad because a real 6230 has a
 * numeric keypad under the screen gets the same answer it would on the device.
 * On a desktop that means the game's own soft keypad stays hidden, so the
 * profile is opt-in.
 */
static const nj_device_prop nokia6230_props[] = {
    { "microedition.platform",     "Nokia6230/05.50" },
    { "com.nokia.mid.imei",        NJ_ID_IMEI },
    { "com.nokia.IMEI",            NJ_ID_IMEI },
    { "com.nokia.mid.imsi",        NJ_ID_IMSI },
    { "com.nokia.IMSI",            NJ_ID_IMSI },
    { "com.nokia.mid.mobinfo.IMSI", NJ_ID_IMSI },
    { "com.nokia.mid.mcc",         NJ_ID_MCC },
    { "com.nokia.mid.mnc",         NJ_ID_MNC },
    { "com.nokia.mid.networkID",   NJ_ID_NET },
    { "com.nokia.keyboard.type",   "PhoneKeypad" },
    { "com.nokia.mid.ui.version",  "1.1" },
};

/*
 * Sony Ericsson K750i.  The platform string carries model and firmware the way
 * the real one does, and the network keys are the `com.sonyericsson.net.*`
 * family a title uses to decide whether it is on a home network before it
 * dials anything.
 */
static const nj_device_prop sonyericsson_props[] = {
    { "microedition.platform",       "SonyEricssonK750i/R1J002" },
    { "com.sonyericsson.imei",       NJ_ID_IMEI },
    { "com.sonyericsson.IMEI",       NJ_ID_IMEI },
    { "com.sonyericsson.imsi",       NJ_ID_IMSI },
    { "com.sonyericsson.IMSI",       NJ_ID_IMSI },
    { "com.sonyericsson.net.mcc",    NJ_ID_MCC },
    { "com.sonyericsson.net.mnc",    NJ_ID_MNC },
    { "com.sonyericsson.net.lac",    "9999" },
    { "com.sonyericsson.net.cellid", "12345" },
};

/*
 * Motorola E398.  Motorola's platform string is the one handset in the corpus
 * that a title matches EXACTLY rather than by prefix, which is why the profile
 * is spelled the way the device spells it and not "Motorola E398".
 */
static const nj_device_prop motorola_props[] = {
    { "microedition.platform", "Motorola-E398" },
    { "com.motorola.IMEI",     NJ_ID_IMEI },
    { "com.motorola.imei",     NJ_ID_IMEI },
    { "com.motorola.imsi",     NJ_ID_IMSI },
    { "com.motorola.IMSI",     NJ_ID_IMSI },
};

/* Samsung SGH-E700, whose platform string a title strips to "SAMSUNG". */
static const nj_device_prop samsung_props[] = {
    { "microedition.platform", "SAMSUNG-SGH-E700/1.0" },
    { "com.samsung.imei",      NJ_ID_IMEI },
    { "com.samsung.IMEI",      NJ_ID_IMEI },
    { "com.samsung.imsi",      NJ_ID_IMSI },
};

/* Siemens S65, the third handset family the corpus probes for identity. */
static const nj_device_prop siemens_props[] = {
    { "microedition.platform", "Siemens-S65/50" },
    { "com.siemens.imei",      NJ_ID_IMEI },
    { "com.siemens.IMEI",      NJ_ID_IMEI },
    { "com.siemens.imsi",      NJ_ID_IMSI },
    { "com.siemens.IMSI",      NJ_ID_IMSI },
};

/*
 * The empty profile is the default, and it is empty on purpose: with it the
 * runtime answers exactly what this port is and nothing else, so a title that
 * runs here runs because the runtime works and not because a handset was
 * imitated.  It is also the row that keeps `--prop` alone useful - override
 * one key without adopting a whole device.
 */
static const nj_device devices[] = {
    { "generic", "no profile: this port's own identity, and no vendor keys",
      NULL, 0 },
    { "nokia6230", "Nokia 6230 / Series 40 (Nokia platform, IMEI, keypad)",
      nokia6230_props, NJ_DEVICE_N(nokia6230_props) },
    { "sonyericsson-k750", "Sony Ericsson K750i (SonyEricsson platform, IMEI)",
      sonyericsson_props, NJ_DEVICE_N(sonyericsson_props) },
    { "motorola-e398", "Motorola E398 (Motorola platform, IMEI, soft keys)",
      motorola_props, NJ_DEVICE_N(motorola_props) },
    { "samsung-sgh-e700", "Samsung SGH-E700 (SAMSUNG platform, IMEI)",
      samsung_props, NJ_DEVICE_N(samsung_props) },
    { "siemens-s65", "Siemens S65 (Siemens platform, IMEI)",
      siemens_props, NJ_DEVICE_N(siemens_props) },
};

#define NJ_NDEVICES NJ_DEVICE_N(devices)

nj_u4 nj_device_count(void)
{
    return (nj_u4)NJ_NDEVICES;
}

const nj_device *nj_device_at(nj_u4 index)
{
    if (index >= (nj_u4)NJ_NDEVICES) {
        return NULL;
    }
    return &devices[index];
}

const nj_device *nj_device_find(const char *name)
{
    nj_u4 i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < (nj_u4)NJ_NDEVICES; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}

const nj_device *nj_device_default(void)
{
    return &devices[0];
}

void nj_vm_set_device(nj_vm *vm, const nj_device *device)
{
    if (vm == NULL) {
        return;
    }
    if (device == NULL) {
        device = nj_device_default();
    }
    vm->sys_props = device->props;
    vm->nsys_props = device->count;
    vm->motorola_softkeys = device->name != NULL &&
                            strcmp(device->name, "motorola-e398") == 0;
}

void nj_vm_set_system_properties(nj_vm *vm, const nj_device_prop *props,
                                 nj_u4 count)
{
    if (vm == NULL) {
        return;
    }
    vm->sys_props = props;
    vm->nsys_props = props != NULL ? count : 0u;
}
