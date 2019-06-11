#include <mruby.h>
#include <mruby/class.h>
#include <mruby/variable.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/value.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <mruby/proc.h>
#include <stdlib.h>

#if MRUBY_RELEASE_NO < 20002 && !defined(mrb_true_p)
# define RIStruct RIstruct
#endif

#define id_WeakRef  mrb_intern_lit(mrb, "WeakRef")
#define id_RefError mrb_intern_lit(mrb, "RefError")
#define id_backref  mrb_intern_lit(mrb, "backref@mruby-weakref")

#define C_WEAKREF   mrb_class_get(mrb, "WeakRef")
#define E_REFERROR  mrb_class_get_under(mrb, C_WEAKREF, "RefError")

#ifndef E_STANDARD_ERROR
# define E_STANDARD_ERROR mrb_exc_get(mrb, "StandardError")
#endif

#define AUX_DATA_MALLOC(M, C, S, T, D)                          \
        ((S*)aux_data_malloc((M), (C), sizeof(S), (T), (D)))    \

static void *
aux_data_malloc(mrb_state *mrb, struct RClass *c, size_t size, const mrb_data_type *t, struct RData **d)
{
  *d = mrb_data_object_alloc(mrb, c, NULL, t);
  (*d)->data = mrb_calloc(mrb, 1, size);
  return (*d)->data;
}

static void
aux_ary_delete_at(mrb_state *mrb, struct RArray *ary, mrb_int idx)
{
  mrb_int len = ARY_LEN(ary);
  if (idx < 0) { idx += len; }
  if (idx < 0 || idx >= len) { return; }

  mrb_int pivot = idx + 1;
  if (pivot < len) {
    mrb_value *p = (mrb_value *)ARY_PTR(ary);
    memmove(p + idx, p + pivot, sizeof(mrb_value) * (len - pivot));
  }

  mrb_ary_pop(mrb, mrb_obj_value(ary));
}

static void
aux_ary_delete_once(mrb_state *mrb, struct RArray *ary, mrb_value obj)
{
  const mrb_value *p = ARY_PTR(ary);
  mrb_int len = ARY_LEN(ary);
  for (mrb_int i = 0; i < len; i ++) {
    if (mrb_obj_eq(mrb, obj, p[i])) {
      aux_ary_delete_at(mrb, ary, i);
      break;
    }
  }
}

static mrb_value
aux_proc_from_method(mrb_state *mrb, mrb_method_t m)
{
  struct RProc *proc;

  if (MRB_METHOD_FUNC_P(m)) {
    proc = mrb_proc_new_cfunc(mrb, MRB_METHOD_FUNC(m));
  } else {
    proc = MRB_METHOD_PROC(m);
  }

  return mrb_obj_value(proc);
}

static mrb_value
aux_yield_super(mrb_state *mrb, mrb_value self, mrb_int argc, const mrb_value *argv, struct RClass **c)
{
  int ai = mrb_gc_arena_save(mrb);
  mrb_sym mid = mrb_get_mid(mrb);

  if (mid < 1) {
nomethod_err:
    mrb_raise(mrb, E_NOMETHOD_ERROR,
              "super: no superclass method");
  }

  struct RClass *dummy_c;
  if (c == NULL) { dummy_c = NULL; c = &dummy_c; }

  if (*c == NULL) { *c = mrb_class(mrb, self); }

  mrb_method_t m = mrb_method_search_vm(mrb, c, mid);
  if (MRB_METHOD_UNDEF_P(m) || *c == NULL) { goto nomethod_err; }

  *c = (*c)->super;
  if (c == NULL) { goto nomethod_err; }

  m = mrb_method_search_vm(mrb, c, mid);
  if (MRB_METHOD_UNDEF_P(m) || *c == NULL) { goto nomethod_err; }

  mrb_value ret = mrb_yield_with_class(mrb, aux_proc_from_method(mrb, m), argc, argv, self, *c);
  mrb_gc_arena_restore(mrb, ai);
  mrb_gc_protect(mrb, ret);

  return ret;
}

/*
 * class WeakRef
 */

struct capture
{
  struct RData *backref;
  mrb_value target;
};

static void
free_weakref(mrb_state *mrb, void *ptr)
{
  struct RData *p = (struct RData *)ptr;

  if (p) {
    struct capture *cap = (struct capture *)p->data;
    if (cap) {
      cap->backref = NULL;

      if (!mrb_object_dead_p(mrb, (struct RBasic *)p)) {
        /* capture オブジェクトが生存しているので、その先にある循環参照を切る */
        struct RClass *c = mrb_class(mrb, cap->target);
        if (c->tt == MRB_TT_SCLASS) {
          mrb_value backrefs = mrb_obj_iv_get(mrb, (struct RObject *)c, id_backref);
          if (mrb_type(backrefs) == MRB_TT_ARRAY) {
            aux_ary_delete_once(mrb, mrb_ary_ptr(backrefs), mrb_obj_value(p));
          }
        }
      }
    }
  }
}

static const mrb_data_type weakref_data_type = { "WeakRef@mruby-weakref", free_weakref };

static void
free_capture(mrb_state *mrb, void *ptr)
{
  struct capture *p = (struct capture *)ptr;

  if (p) {
    if (p->backref) {
      p->backref->data = NULL;
    }

    mrb_free(mrb, p);
  }
}

static const mrb_data_type capture_data_type = { "WeakRef::Capture@mruby-weakref", free_capture };

static void
delink_weakref(mrb_state *mrb, struct RData *capture)
{
  struct capture *capturep = ((struct capture *)capture->data);
  mrb_value target = capturep->target;
  capturep->backref = NULL;
  if (MRB_FROZEN_P(mrb_obj_ptr(target))) { return; }
  mrb_value sclass = mrb_singleton_class(mrb, target);
  if (mrb_type(sclass) == MRB_TT_SCLASS) {
    mrb_value backrefs = mrb_iv_get(mrb, sclass, id_backref);
    if (mrb_type(backrefs) == MRB_TT_ARRAY) {
      aux_ary_delete_once(mrb, mrb_ary_ptr(backrefs), mrb_obj_value(capture));
    }
  }
}

static mrb_value
weakref_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value target;
  mrb_get_args(mrb, "o", &target);

  if (mrb_type(self) != MRB_TT_DATA) {
    /* 例外を起こす */
    mrb_check_type(mrb, self, MRB_TT_DATA);
  }

  struct RData *weakref = RDATA(self);
  if (weakref->type != NULL) {
    struct RData *tmp = (struct RData *)mrb_data_get_ptr(mrb, self, &weakref_data_type);
    delink_weakref(mrb, tmp);
  }

  mrb_value backrefs;
  mrb_value sclass = mrb_singleton_class(mrb, target);
  if (mrb_type(sclass) == MRB_TT_SCLASS) {
      backrefs = mrb_iv_get(mrb, sclass, id_backref);
      if (mrb_type(backrefs) == MRB_TT_ARRAY) {
        mrb_ary_modify(mrb, mrb_ary_ptr(backrefs));
    } else {
        backrefs = mrb_ary_new_capa(mrb, 1);
        mrb_iv_set(mrb, sclass, id_backref, backrefs);
    }
  } else {
    backrefs = mrb_nil_value();
  }

  struct RData *capture;
  struct capture *capturep = AUX_DATA_MALLOC(mrb, mrb->object_class, struct capture, &capture_data_type, &capture);
  capturep->target = target;
  capturep->backref = weakref;
  if (!mrb_nil_p(backrefs)) { mrb_ary_push(mrb, backrefs, mrb_obj_value(capture)); }

  mrb_data_init(self, capture, &weakref_data_type);

#if MRUBY_RELEASE_NO < 20002 && !defined(mrb_true_p)
  /* "new" になっているので、"initialize" に書き換える */
  mrb->c->ci->mid = mrb_intern_lit(mrb, "initialize");
#endif

  struct RClass *c = C_WEAKREF;
  mrb_value ret = aux_yield_super(mrb, self, 1, &target, &c);

#if MRUBY_RELEASE_NO < 20002 && !defined(mrb_true_p)
  /* "initialize" に書き換えていたので、"new" に戻す */
  mrb->c->ci->mid = mrb_intern_lit(mrb, "new");
#endif

  return ret;
}

static mrb_value
weakref_setobj(mrb_state *mrb, mrb_value self)
{
  mrb_value target;
  mrb_get_args(mrb, "o", &target);

  /* 何もせずに戻る */

  return self;
}

static mrb_value
weakref_getobj_trial(mrb_state *mrb, mrb_value self, mrb_bool *alive)
{
  *alive = FALSE;

  struct RData *capture = (struct RData *)mrb_data_get_ptr(mrb, self, &weakref_data_type);
  if (capture == NULL) {
    return mrb_nil_value();
  }

  struct capture *capturep = (struct capture *)capture->data;
  if (capturep == NULL) {
    return mrb_nil_value();
  }

  mrb_value target = capturep->target;

  switch (mrb->gc.state) {
  case MRB_GC_STATE_SWEEP:
    if (mrb_object_dead_p(mrb, (struct RBasic *)capture) ||
        mrb_object_dead_p(mrb, mrb_basic_ptr(target))) {
      DATA_PTR(self) = NULL;
      return mrb_nil_value();
    }
    break;
  case MRB_GC_STATE_MARK:
    mrb_gc_mark(mrb, (struct RBasic *)capture);
    mrb_gc_mark(mrb, mrb_basic_ptr(target));
    break;
  default:
    break;
  }

  *alive = TRUE;

  return target;
}

static mrb_value
weakref_obj_getobj(mrb_state *mrb, mrb_value self)
{
  mrb_bool alive;
  mrb_value target = weakref_getobj_trial(mrb, self, &alive);

  if (!alive) {
    mrb_raise(mrb, E_REFERROR, "Invalid Reference - probably recycled");
  }

  mrb_gc_protect(mrb, target);

  return target;
}

static mrb_value
weakref_getobj(mrb_state *mrb, mrb_value self)
{
  mrb_get_args(mrb, "", NULL);
  return weakref_obj_getobj(mrb, self);
}

static mrb_value
weakref_obj_alive_p(mrb_state *mrb, mrb_value self)
{
  mrb_bool alive;
  weakref_getobj_trial(mrb, self, &alive);

  return alive ? mrb_true_value() : mrb_nil_value();
}

static mrb_value
weakref_alive_p(mrb_state *mrb, mrb_value self)
{
  mrb_get_args(mrb, "", NULL);
  return weakref_obj_alive_p(mrb, self);
}

static mrb_value
weakref_s_getobj(mrb_state *mrb, mrb_value self)
{
  mrb_value ref;
  mrb_get_args(mrb, "o", &ref);

  if (mrb_data_check_get_ptr(mrb, ref, &weakref_data_type) == NULL) {
    /* weakref オブジェクトではないので、常に「生きている」オブジェクトである */
    return ref;
  } else {
    return weakref_obj_getobj(mrb, ref);
  }
}

static mrb_value
weakref_s_alive_p(mrb_state *mrb, mrb_value self)
{
  mrb_value ref;
  mrb_get_args(mrb, "o", &ref);

  if (mrb_data_check_get_ptr(mrb, ref, &weakref_data_type) == NULL) {
    /* weakref オブジェクトではないので、常に「生きている」オブジェクトである */
    return mrb_true_value();
  } else {
    return weakref_obj_alive_p(mrb, ref);
  }
}

void
mrb_mruby_weakref_gem_init(mrb_state *mrb)
{
  mrb_intern_lit(mrb, "WeakRef");
  mrb_intern_lit(mrb, "RefError");
  mrb_intern_lit(mrb, "__initialize__");
  mrb_intern_lit(mrb, "weakref_alive?");

  struct RClass *weakref = mrb_define_class(mrb, "WeakRef", mrb_class_get(mrb, "Delegator"));
  MRB_SET_INSTANCE_TT(weakref, MRB_TT_DATA);
  mrb_define_method(mrb, weakref, "initialize", weakref_initialize, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, weakref, "__setobj__", weakref_setobj, MRB_ARGS_REQ(1));
  mrb_define_method(mrb, weakref, "__getobj__", weakref_getobj, MRB_ARGS_NONE());
  mrb_define_method(mrb, weakref, "weakref_alive?", weakref_alive_p, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, weakref, "getobj", weakref_s_getobj, MRB_ARGS_NONE());
  mrb_define_class_method(mrb, weakref, "alive?", weakref_s_alive_p, MRB_ARGS_NONE());

  struct RClass *referr = mrb_define_class_under(mrb, weakref, "RefError", E_STANDARD_ERROR);
  (void)referr;
}

void
mrb_mruby_weakref_gem_final(mrb_state *mrb)
{
}
