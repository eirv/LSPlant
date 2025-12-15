#include "xdl_ext.h"

#include "xdl.c"
#include "xdl_util.h"

void *xdl_dsym_prefix(void *handle, const char *symbol, size_t *symbol_size) {
  if (NULL == handle || NULL == symbol) return NULL;
  if (NULL != symbol_size) *symbol_size = 0;

  xdl_t *self = (xdl_t *)handle;

  // load .symtab only once
  if (!self->symtab_try_load) {
    self->symtab_try_load = true;
    if (0 != xdl_symtab_load(self)) return NULL;
  }

  // find symbol
  if (NULL == self->symtab) return NULL;
  size_t symbol_len = strlen(symbol);
  for (size_t i = 0; i < self->symtab_cnt; i++) {
    ElfW(Sym) *sym = self->symtab + i;

    if (!XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx)) continue;
    // if (0 != strncmp(self->strtab + sym->st_name, symbol, self->strtab_sz - sym->st_name)) continue;
    if (!xdl_util_starts_with(self->strtab + sym->st_name, symbol)) continue;

    if (NULL != symbol_size) *symbol_size = sym->st_size;
    return (void *)(self->load_bias + sym->st_value);
  }

  return NULL;
}
