

#include "pub_tool_basics.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcproc.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_oset.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_xarray.h"
#include "pub_tool_clientstate.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_machine.h"      // VG_(fnptr_to_fnentry)

#include "cv_hashtable.h"

void insert_entry_HT (VgHashTable* table, Addr key, HChar* function_name)
{

   entry* func_name_record;

   func_name_record = VG_(malloc) ("entry", sizeof(entry));
   func_name_record->address = key;
   func_name_record->function_name = VG_(strdup)("function_name", function_name);
   func_name_record->count = 0;
   VG_(HT_add_node)(table, (VgHashNode*)func_name_record);

}

void print_HT (VgHashTable* table)
{
   entry* func_name_record;

   VG_(HT_ResetIter)(table);

   VG_(umsg) ("All function calls:\n");
   while ((func_name_record = VG_(HT_Next)(table)) != NULL) {
      VG_(umsg) ("\t\t%s: %d call(s)\n", func_name_record->function_name, func_name_record->count);
   }
}
