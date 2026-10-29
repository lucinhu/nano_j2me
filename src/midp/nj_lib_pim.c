/*
 * nano_j2me - src/midp/nj_lib_pim.c
 *
 * JSR 75's personal information management half: javax.microedition.pim.
 *
 * WHY THIS EXISTS AT ALL
 * ----------------------
 * A title that names a PIM class and cannot find it dies with
 * NoClassDefFoundError inside its own feature code.  3D孤胆枪手2010_4634 does
 * exactly the phone-book thing:
 *
 *     PIM.getInstance().openPIMList(PIM.CONTACT_LIST, PIM.READ_ONLY)
 *         -> checkcast ContactList -> isSupportedField() / items() / close()
 *
 * and a fresh phone answers that with an empty contact list, not with a link
 * error.  This runtime has no contact database and deliberately does not invent
 * one: the STORE is a port feature (a phone's contacts, a vCard file, a
 * RecordStore), and nothing in this project asks for one yet.  What a device
 * without a populated store can honestly offer is what is here:
 *
 *   - PIM.getInstance() returns a PIM object;
 *   - openPIMList(CONTACT_LIST, mode) returns an EMPTY ContactList: no field is
 *     supported, items() is an empty Enumeration, close() does nothing;
 *   - a list type this build does not have (event, todo) raises PIMException,
 *     which is what the specification defines for a list that is not found;
 *   - a mode outside READ_ONLY/WRITE_ONLY/READ_WRITE raises
 *     IllegalArgumentException, again per the specification.
 *
 * WHY THE CLASSES ARE SPELLED THROUGH THEIR PARENTS
 * -------------------------------------------------
 * A built-in class here carries one super and no interface list, so is-a is
 * spelled as the parent (see implements_interface() in nj_class.c).  That is
 * why PIMList and Contact declare the methods a caller resolves THROUGH them
 * rather than relying on inheritance: interface method resolution looks in the
 * interface's own table.
 *
 * WHAT IS NOT IMPLEMENTED
 * -----------------------
 * Contact/Event/Todo values (getString, addString, ...), Field labels, serial
 * formats (vCard/vCal) and write-back.  isSupportedField() answers false for
 * every field, so a title that browses an empty phone book never reaches them;
 * a title that tries to write gets UnsupportedOperationException from the
 * item-level methods it cannot create, and openPIMList with a write mode is
 * still allowed because an empty list may legitimately be opened read-write.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_PIM        "javax/microedition/pim/PIM"
#define NJ_C_PIMEXC     "javax/microedition/pim/PIMException"
#define NJ_C_PIMLIST    "javax/microedition/pim/PIMList"
#define NJ_C_CONTACTL   "javax/microedition/pim/ContactList"
#define NJ_C_CONTACTL_I "javax/microedition/pim/ContactListImpl"
#define NJ_C_PIMITEM    "javax/microedition/pim/PIMItem"
#define NJ_C_CONTACT    "javax/microedition/pim/Contact"
#define NJ_C_UNSUPF     "javax/microedition/pim/UnsupportedFieldException"

/* JSR 75's list types and modes. */
#define PIM_CONTACT_LIST 1
#define PIM_EVENT_LIST   2
#define PIM_TODO_LIST    3
#define PIM_READ_ONLY    1
#define PIM_WRITE_ONLY   2
#define PIM_READ_WRITE   3

/* ------------------------------------------------------------------ */
/* Natives                                                             */
/* ------------------------------------------------------------------ */
/*
 * PIM.getInstance().  The specification calls it a singleton; the object holds
 * no state, so a fresh instance per call is indistinguishable and keeps the
 * "no writable globals" rule.
 */
NJFN(pim_get_instance)
{
    NJ_UNUSED(args);
    ret->r = nj_object_new(vm, NJ_C_PIM);
    return ret->r ? NJ_OK : NJ_ERR;
}

/* PIM.openPIMList(pimListType, mode). */
NJFN(pim_open_list)
{
    nj_s4 type = NJ_ARG_INT(args, 0);
    nj_s4 mode = NJ_ARG_INT(args, 1);

    if (mode != PIM_READ_ONLY && mode != PIM_WRITE_ONLY
        && mode != PIM_READ_WRITE) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "mode must be READ_ONLY, WRITE_ONLY or READ_WRITE");
    }
    if (type != PIM_CONTACT_LIST) {
        /*
         * This build carries the contact list only.  "List not found" is the
         * specification's answer, and it is the truth here: there is no event
         * or todo store to open.
         */
        return nj_throw(vm, NJ_C_PIMEXC, "no list of that type");
    }
    ret->r = nj_object_new(vm, NJ_C_CONTACTL_I);
    return ret->r ? NJ_OK : NJ_ERR;
}

/* PIM.listPIMLists(type): the names of the lists of that type. */
NJFN(pim_list_lists)
{
    nj_s4 type = NJ_ARG_INT(args, 0);
    nj_ref arr;

    if (type != PIM_CONTACT_LIST) {
        arr = nj_array_new(vm, "java/lang/String", 0u);
        if (!arr) {
            return NJ_ERR;
        }
        ret->r = arr;
        return NJ_OK;
    }
    /* One list, named like the phone-book list a device shows. */
    arr = nj_array_new(vm, "java/lang/String", 1u);
    if (!arr) {
        return NJ_ERR;
    }
    arr->u.refs[0] = nj_string_new(vm, "Contacts");
    ret->r = arr;
    return NJ_OK;
}

/* ContactList.getName(). */
NJFN(contactlist_get_name)
{
    NJ_UNUSED(args);
    ret->r = nj_string_new(vm, "Contacts");
    return ret->r ? NJ_OK : NJ_ERR;
}

/* ContactList.items(): an Enumeration of the contacts in the list. */
NJFN(contactlist_items)
{
    nj_ref v;

    NJ_UNUSED(args);
    v = nj_object_new(vm, "java/util/Vector");
    if (!v) {
        return NJ_ERR;
    }
    ret->r = nj_call_ref(vm, v, "java/util/Vector", "elements",
                         "()Ljava/util/Enumeration;", NULL);
    return ret->r ? NJ_OK : NJ_ERR;
}

/* ContactList.isSupportedField(field): this store holds no fields at all. */
NJFN(contactlist_supported)
{
    NJ_UNUSED(args);
    ret->i = 0;
    return NJ_OK;
}

/* ContactList.getFieldLabel(field): nothing to label. */
NJFN(contactlist_field_label)
{
    NJ_UNUSED(args);
    ret->r = NULL;
    return NJ_OK;
}

NJFN(contactlist_close)
{
    NJ_UNUSED(args);
    NJ_UNUSED(ret);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field pim_fields[] = {
    FLD("CONTACT_LIST", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("EVENT_LIST",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("TODO_LIST",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("READ_ONLY",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("WRITE_ONLY",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("READ_WRITE",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method pim_methods[] = {
    M("getInstance", "()L" NJ_C_PIM ";", NJ_ACC_STATIC),
    M("openPIMList", "(II)L" NJ_C_PIMLIST ";", 0),
    M("listPIMLists", "(I)[Ljava/lang/String;", 0)
};

/*
 * PIMList declares the methods a MIDlet calls through the interface.  A
 * ContactList reference resolves in ContactList's own table (see the note at
 * the top), so its table repeats the union it exposes.
 */
static const nj_builtin_method pimlist_methods[] = {
    M("close",           "()V", NJ_ACC_ABSTRACT),
    M("getName",         "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("items",           "()Ljava/util/Enumeration;", NJ_ACC_ABSTRACT),
    M("isSupportedField", "(I)Z", NJ_ACC_ABSTRACT),
    M("getFieldLabel",   "(I)Ljava/lang/String;", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method contactlist_methods[] = {
    M("close",           "()V", NJ_ACC_ABSTRACT),
    M("getName",         "()Ljava/lang/String;", NJ_ACC_ABSTRACT),
    M("items",           "()Ljava/util/Enumeration;", NJ_ACC_ABSTRACT),
    M("isSupportedField", "(I)Z", NJ_ACC_ABSTRACT),
    M("getFieldLabel",   "(I)Ljava/lang/String;", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method contactlist_impl_methods[] = {
    M("close",           "()V", 0),
    M("getName",         "()Ljava/lang/String;", 0),
    M("items",           "()Ljava/util/Enumeration;", 0),
    M("isSupportedField", "(I)Z", 0),
    M("getFieldLabel",   "(I)Ljava/lang/String;", 0)
};

static const nj_builtin_method pimitem_methods[] = {
    M("getPIMList", "()L" NJ_C_PIMLIST ";", NJ_ACC_ABSTRACT)
};

static const nj_builtin_method contact_methods[] = {
    M("getPIMList",   "()L" NJ_C_PIMLIST ";", NJ_ACC_ABSTRACT),
    M("countValues",  "(I)I", NJ_ACC_ABSTRACT),
    M("getString",    "(II)Ljava/lang/String;", NJ_ACC_ABSTRACT)
};

const nj_builtin_class nj_lib_pim_classes[] = {
    /* A PIMException: PIMException and UnsupportedFieldException carry no
     * methods of their own; a MIDlet catches them, and the constructors come
     * from the JDK exception chain this runtime provides. */
    { NJ_C_PIMEXC, "java/lang/Exception", NJ_ACC_PUBLIC,
      NULL, 0u, NULL, 0u },
    { NJ_C_UNSUPF, "java/lang/RuntimeException", NJ_ACC_PUBLIC,
      NULL, 0u, NULL, 0u },
    { NJ_C_PIM, "java/lang/Object", NJ_ACC_PUBLIC,
      pim_fields,
      (nj_u2)(sizeof(pim_fields) / sizeof(pim_fields[0])),
      pim_methods,
      (nj_u2)(sizeof(pim_methods) / sizeof(pim_methods[0])) },
    { NJ_C_PIMLIST, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, pimlist_methods,
      (nj_u2)(sizeof(pimlist_methods) / sizeof(pimlist_methods[0])) },
    { NJ_C_CONTACTL, NJ_C_PIMLIST,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, contactlist_methods,
      (nj_u2)(sizeof(contactlist_methods) / sizeof(contactlist_methods[0])) },
    { NJ_C_CONTACTL_I, NJ_C_CONTACTL, NJ_ACC_PUBLIC,
      NULL, 0u, contactlist_impl_methods,
      (nj_u2)(sizeof(contactlist_impl_methods)
              / sizeof(contactlist_impl_methods[0])) },
    { NJ_C_PIMITEM, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, pimitem_methods,
      (nj_u2)(sizeof(pimitem_methods) / sizeof(pimitem_methods[0])) },
    { NJ_C_CONTACT, NJ_C_PIMITEM,
      NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, contact_methods,
      (nj_u2)(sizeof(contact_methods) / sizeof(contact_methods[0])) }
};
const int nj_lib_pim_class_count =
    (int)(sizeof(nj_lib_pim_classes) / sizeof(nj_lib_pim_classes[0]));

const nj_native_entry nj_lib_pim_table[] = {
    { NJ_C_PIM, "getInstance", "()L" NJ_C_PIM ";", pim_get_instance },
    { NJ_C_PIM, "openPIMList", "(II)L" NJ_C_PIMLIST ";", pim_open_list },
    { NJ_C_PIM, "listPIMLists", "(I)[Ljava/lang/String;", pim_list_lists },
    { NJ_C_CONTACTL_I, "getName", "()Ljava/lang/String;", contactlist_get_name },
    { NJ_C_CONTACTL_I, "items", "()Ljava/util/Enumeration;", contactlist_items },
    { NJ_C_CONTACTL_I, "isSupportedField", "(I)Z", contactlist_supported },
    { NJ_C_CONTACTL_I, "getFieldLabel", "(I)Ljava/lang/String;",
      contactlist_field_label },
    { NJ_C_CONTACTL_I, "close", "()V", contactlist_close }
};
const int nj_lib_pim_count =
    (int)(sizeof(nj_lib_pim_table) / sizeof(nj_lib_pim_table[0]));
