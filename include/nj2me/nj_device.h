/*
 * nano_j2me - nj_device.h
 *
 * The device a MIDlet believes it is running on.
 *
 * ---------------------------------------------------------------------------
 * WHY THE RUNTIME HAS TO KNOW
 * ---------------------------------------------------------------------------
 * System.getProperty("microedition.platform") is how a J2ME title decides which
 * handset it is on, and a great many of them decide badly: they ask, get a
 * string they do not recognise, and take a path that was never finished.  Some
 * refuse to start at all - 1945空战英雄_6105 asks for a platform that starts with
 * "Nokia" and stops with an IllegalAccessException when it does not get one.
 *
 * The keys that matter are not only the platform string.  The collected corpus
 * probes the handset's identity far more often than its platform:
 *
 *     microedition.platform     71 titles
 *     com.sonyericsson.imei     14
 *     com.nokia.mid.imei        13
 *     com.nokia.IMEI            12
 *     com.samsung.imei          12
 *     com.nokia.mid.imsi         8
 *     com.siemens.IMEI           8
 *     ... and a dozen more spellings of IMEI/IMSI/MCC/MNC
 *
 * A real device answers all of them; a runtime that answers NULL leaves the
 * MIDlet holding a null it never checks for.  So a device profile is a TABLE OF
 * SYSTEM PROPERTIES with a name, and `nj2me --device nokia6230` is "pretend to
 * be this handset".
 *
 * ---------------------------------------------------------------------------
 * WHAT THE VALUES ARE, AND WHAT THEY ARE NOT
 * ---------------------------------------------------------------------------
 * The IMEI/IMSI numbers in these profiles are synthetic: a valid length and a
 * valid Luhn check digit, and a TAC and MCC/MNC that belong to nobody.  They are
 * there so a title that reads an identity - or parses one - sees something
 * shaped like an identity.  They do not identify this host, its owner, or any
 * subscriber, and the profiles say so where they are defined.
 */
#ifndef NJ_DEVICE_H
#define NJ_DEVICE_H

#include "nj_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nj_vm nj_vm;

/* One System.getProperty() entry.  Both strings are borrowed, never freed. */
typedef struct nj_device_prop {
    const char *key;
    const char *value;
} nj_device_prop;

/*
 * A named handset.  `props` is a lookup table: when a key appears more than
 * once the LAST entry wins, which is what lets an embedder install a profile
 * and then put its own overrides on top of the same table.
 */
typedef struct nj_device {
    const char *name;             /* what --device takes, e.g. "nokia6230" */
    const char *summary;          /* one line, for --list-devices           */
    const nj_device_prop *props;  /* may be NULL when the table is empty    */
    nj_u4 count;
} nj_device;

/* The built-in profiles, in the order --list-devices prints them. */
nj_u4            nj_device_count(void);
const nj_device *nj_device_at(nj_u4 index);
const nj_device *nj_device_find(const char *name);   /* NULL when unknown */
const nj_device *nj_device_default(void);            /* "generic" */

/*
 * Hand the VM the properties it answers.  Both are optional and both borrow
 * their memory: the table must outlive the VM, which is how a caller can pass
 * an array of pointers into argv.
 *
 * nj_vm_set_device() installs one profile's table; nj_vm_set_system_properties()
 * installs an arbitrary one (device profile first, then overrides - last match
 * wins). Setting either replaces the property table. The built-in
 * motorola-e398 profile also selects -21/-22 for Canvas soft-key callbacks;
 * other profiles (including NULL) restore the usual -6/-7 convention.
 * nj_vm_set_system_properties() preserves that choice so property overrides
 * can follow profile selection. Directional and numeric keys keep their
 * normal MIDP codes; built-in widgets and Command dispatch use logical keys.
 */
void nj_vm_set_device(nj_vm *vm, const nj_device *device);
void nj_vm_set_system_properties(nj_vm *vm, const nj_device_prop *props,
                                 nj_u4 count);

#ifdef __cplusplus
}
#endif

#endif /* NJ_DEVICE_H */
