
/*--------------------------------------------------------------------*/
/*--- Covergrind - code coverage tool                    cv_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This is an experimental tool. This program is a modified Cachegrind tool.
*/

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

HChar* src_dir               = ""; /*fix this*/
static Bool src_dir_found    = False;

static const HChar* clo_covergrind_out_file = "covergrind.out.%p";

const HChar *fnname;

VgHashTable* func_calls_HT = NULL;

typedef
   struct {
      ULong a;  /* total # memory accesses of this kind */
   }
   CacheCC;

typedef
   struct {
      ULong b;  /* total # branches of this kind */
   }
   BranchCC;

//------------------------------------------------------------
// Primary data structure #1: CC table
// - Holds the per-source-line hit/miss stats, grouped by file/function/line.
// - an ordered set of CCs.  CC indexing done by file/function/line (as
//   determined from the instrAddr).
// - Traversed for dumping stats at end in file/func/line hierarchy.

typedef struct {
   HChar* file;
   const HChar* fn;
   Int    line;
}
CodeLoc;

typedef struct {
   CodeLoc  loc; /* Source location that these counts pertain to */
   CacheCC  Ir;  /* Insn read counts */
   ULong      instr_any; /*Any access*/
   BranchCC Bc;  /* Conditional branch counts */
} LineCC;



// First compare file, then fn, then line.
static Word cmp_CodeLoc_LineCC(const void *vloc, const void *vcc)
{
   Word res;
   const CodeLoc* a = (const CodeLoc*)vloc;
   const CodeLoc* b = &(((const LineCC*)vcc)->loc);

   res = VG_(strcmp)(a->file, b->file);
   if (0 != res)
      return res;

   res = VG_(strcmp)(a->fn, b->fn);
   if (0 != res)
      return res;

   return a->line - b->line;
}

static OSet* CC_table;

//------------------------------------------------------------
// Primary data structure #2: InstrInfo table
// - Holds the cached info about each instr that is used for simulation.
// - table(SB_start_addr, list(InstrInfo))
// - For each SB, each InstrInfo in the list holds info about the
//   instruction (instrLen, instrAddr, etc), plus a pointer to its line
//   CC.  This node is what's passed to the simulation function.
// - When SBs are discarded the relevant list(instr_details) is freed.

typedef struct _InstrInfo InstrInfo;
struct _InstrInfo {
   Addr    instr_addr;
   UChar   instr_len;
   ULong   instr_any;
   LineCC* parent;         // parent line-CC
};

typedef struct _SB_info SB_info;
struct _SB_info {
   Addr      SB_addr;      // key;  MUST BE FIRST
   Int       n_instrs;
   Int       n_accessed;
   InstrInfo instrs[0];
};


static void update_count_HT (Addr key)
{
   entry* func_entry;
   if ((func_entry = VG_(HT_lookup)(func_calls_HT, key)) != NULL) {
      func_entry->count++;
   }
}

static OSet* instrInfoTable;

//------------------------------------------------------------
// Secondary data structure: string table
// - holds strings, avoiding dups
// - used for filenames and function names, each of which will be
//   pointed to by one or more CCs.
// - it also allows equality checks just by pointer comparison, which
//   is good when printing the output file at the end.

static OSet* stringTable;

/*------------------------------------------------------------*/
/*--- String table operations                              ---*/
/*------------------------------------------------------------*/

static Word stringCmp( const void* key, const void* elem )
{
   return VG_(strcmp)(*(const HChar *const *)key, *(const HChar *const *)elem);
}

// Get a permanent string;  either pull it out of the string table if it's
// been encountered before, or dup it and put it into the string table.
static HChar* get_perm_string(const HChar* s)
{

   HChar** s_ptr = VG_(OSetGen_Lookup)(stringTable, &s);
   if (s_ptr) {
      return *s_ptr;
   } else {
      HChar** s_node = VG_(OSetGen_AllocNode)(stringTable, sizeof(HChar*));
      *s_node = VG_(strdup)("cg.main.gps.1", s);
      VG_(OSetGen_Insert)(stringTable, s_node);
      return *s_node;
   }
}

/*------------------------------------------------------------*/
/*--- CC table operations                                  ---*/
/*------------------------------------------------------------*/

static void get_debug_info(Addr instr_addr, const HChar **dir,
                           const HChar **file, const HChar **fn, UInt* line)
{
   DiEpoch ep = VG_(current_DiEpoch)();
   Bool found_file_line = VG_(get_filename_linenum)(
                             ep,
                             instr_addr, 
                             file, dir,
                             line
                          );
   Bool found_fn        = VG_(get_fnname)(ep, instr_addr, fn);

   if (!found_file_line) {
      *file = "???";
      *line = 0;
   }
   if (!found_fn) {
      *fn = "???";
   }

}

// Do a three step traversal: by file, then fn, then line.
// Returns a pointer to the line CC, creates a new one if necessary.
static LineCC* get_lineCC(Addr origAddr)
{
   const HChar *fn, *file, *dir;
   UInt    line;
   CodeLoc loc;
   LineCC* lineCC;

   get_debug_info(origAddr, &dir, &file, &fn, &line);

   // Form an absolute pathname if a directory is available
   HChar absfile[VG_(strlen)(dir) + 1 + VG_(strlen)(file) + 1];

   if (dir[0]) {
      VG_(sprintf)(absfile, "%s/%s", dir, file);
   } else {
      VG_(sprintf)(absfile, "%s", file);
   }

   loc.file = absfile;
   loc.fn   = fn;
   loc.line = line;

   /*to record current source directory; used to help isolate counts*/
   if ((0 == VG_(strcmp)(fn, "main")) && (!src_dir_found)) {
      src_dir = loc.file;
      src_dir_found = True;
    }

   lineCC = VG_(OSetGen_Lookup)(CC_table, &loc);
   if (!lineCC) {
      // Allocate and zero a new node.
      lineCC           = VG_(OSetGen_AllocNode)(CC_table, sizeof(LineCC));
      lineCC->loc.file = get_perm_string(loc.file);
      lineCC->loc.fn   = get_perm_string(loc.fn);
      lineCC->loc.line = loc.line;
      lineCC->instr_any = 1;
      lineCC->Ir.a     = 0;
      lineCC->Bc.b     = 0;

      VG_(OSetGen_Insert)(CC_table, lineCC);

      
   }
 
   return lineCC;
}

static VG_REGPARM(1)
void log_1Ir(InstrInfo* n)
{
   n->parent->Ir.a++;
}


static VG_REGPARM(2)
void log_cond_branch(InstrInfo* n, Word taken)
{
   n->parent->Bc.b++;
}


/*------------------------------------------------------------*/
/*--- Instrumentation types and structures                 ---*/
/*------------------------------------------------------------*/

typedef
   IRExpr 
   IRAtom;

typedef 
   enum { 
      Ev_IrGen,  // Generic Ir
      Ev_Bc,     // branch conditional
      Ev_Any,
   }
   EventTag;

typedef
   struct {
      EventTag   tag;
      InstrInfo* inode;
      union {
         struct {
         } IrGen;
         struct {
            IRAtom* taken; /* :: Ity_I1 */
         } Bc;
      } Ev;
   }
   Event;

static void init_Event ( Event* ev ) {
   VG_(memset)(ev, 0, sizeof(Event));
}


/* Up to this many unnotified events are allowed.  Number is
   arbitrary.  Larger numbers allow more event merging to occur, but
   potentially induce more spilling due to extending live ranges of
   address temporaries. */
#define N_EVENTS 16


/* A struct which holds all the running state during instrumentation.
   Mostly to avoid passing loads of parameters everywhere. */
typedef
   struct {
      /* The current outstanding-memory-event list. */
      Event events[N_EVENTS];
      Int   events_used;

      /* The array of InstrInfo bins for the BB. */
      SB_info* sbInfo;

      /* Number InstrInfo bins 'used' so far. */
      Int sbInfo_i;

      /* The output SB being constructed. */
      IRSB* sbOut;
   }
   CgState;


/*------------------------------------------------------------*/
/*--- Instrumentation main                                 ---*/
/*------------------------------------------------------------*/

// Note that origAddr is the real origAddr, not the address of the first
// instruction in the block (they can be different due to redirection).
static
SB_info* get_SB_info(IRSB* sbIn, Addr origAddr)
{
   Int      i, n_instrs;
   IRStmt*  st;
   SB_info* sbInfo;

   // Count number of original instrs in SB
   n_instrs = 0;
   for (i = 0; i < sbIn->stmts_used; i++) {
      st = sbIn->stmts[i];
      if (Ist_IMark == st->tag) n_instrs++;
   }

   // Check that we don't have an entry for this BB in the instr-info table.
   // If this assertion fails, there has been some screwup:  some
   // translations must have been discarded but Covergrind hasn't discarded
   // the corresponding entries in the instr-info table.
   sbInfo = VG_(OSetGen_Lookup)(instrInfoTable, &origAddr);
   tl_assert(NULL == sbInfo);

   // BB never translated before (at this address, at least;  could have
   // been unloaded and then reloaded elsewhere in memory)
   sbInfo = VG_(OSetGen_AllocNode)(instrInfoTable,
                                sizeof(SB_info) + n_instrs*sizeof(InstrInfo)); 
   sbInfo->SB_addr  = origAddr;
   sbInfo->n_instrs = n_instrs;
   sbInfo->n_accessed = 1;
   VG_(OSetGen_Insert)( instrInfoTable, sbInfo );

   return sbInfo;
}

// Reserve and initialise an InstrInfo for the first mention of a new insn.
static
InstrInfo* setup_InstrInfo ( CgState* cgs, Addr instr_addr, UInt instr_len )
{
   InstrInfo* i_node;
   tl_assert(cgs->sbInfo_i >= 0);
   tl_assert(cgs->sbInfo_i < cgs->sbInfo->n_instrs);
   i_node = &cgs->sbInfo->instrs[ cgs->sbInfo_i ];
   i_node->instr_addr = instr_addr;
   i_node->instr_len  = instr_len;
   i_node->parent     = get_lineCC(instr_addr);
   cgs->sbInfo_i++;
   return i_node;
}


/* Generate code for all outstanding memory events, and mark the queue
   empty.  Code is generated into cgs->bbOut, and this activity
   'consumes' slots in cgs->sbInfo. */

static void flushEvents ( CgState* cgs )
{
   Int        i, regparms;
   const HChar* helperName;
   void*      helperAddr;
   IRExpr**   argv;
   IRExpr*    i_node_expr;
   IRDirty*   di;
   Event*     ev;

   i = 0;
   while (i < cgs->events_used) {

      helperName = NULL;
      helperAddr = NULL;
      argv       = NULL;
      regparms   = 0;

      /* generate IR to notify event i and possibly the ones
         immediately following it. */
      tl_assert(i >= 0 && i < cgs->events_used);

      ev  = &cgs->events[i];

      i_node_expr = mkIRExpr_HWord( (HWord)ev->inode );

      /* Decide on helper fn to call and args to pass it, and advance
         i appropriately. */
      switch (ev->tag) {
         case Ev_IrGen:
	       helperName = "log_1Ir";
	       helperAddr = &log_1Ir;
	    argv = mkIRExprVec_1( i_node_expr );
	    regparms = 1;
	    i++;
            break;
         case Ev_Bc:
            helperName = "log_cond_branch";
            helperAddr = &log_cond_branch;
            argv = mkIRExprVec_2( i_node_expr, ev->Ev.Bc.taken );
            regparms = 2;
            i++;
            break;
         default:
            tl_assert(0);
      }

      /* Add the helper. */
      tl_assert(helperName);
      tl_assert(helperAddr);
      tl_assert(argv);
      di = unsafeIRDirty_0_N( regparms, 
                              helperName, VG_(fnptr_to_fnentry)( helperAddr ), 
                              argv );
      addStmtToIRSB( cgs->sbOut, IRStmt_Dirty(di) );
   }

   cgs->events_used = 0;
}

static void addEvent_Ir ( CgState* cgs, InstrInfo* inode )
{
   Event* evt;
   if (cgs->events_used == N_EVENTS)
      flushEvents(cgs);
   tl_assert(cgs->events_used >= 0 && cgs->events_used < N_EVENTS);
   evt = &cgs->events[cgs->events_used];
   init_Event(evt);
   evt->inode    = inode;
      evt->tag = Ev_IrGen;
   cgs->events_used++;
}



static
void addEvent_Bc ( CgState* cgs, InstrInfo* inode, IRAtom* guard )
{
   Event* evt;
   tl_assert(isIRAtom(guard));
   tl_assert(typeOfIRExpr(cgs->sbOut->tyenv, guard) 
             == (sizeof(RegWord)==4 ? Ity_I32 : Ity_I64));
   if (cgs->events_used == N_EVENTS)
      flushEvents(cgs);
   tl_assert(cgs->events_used >= 0 && cgs->events_used < N_EVENTS);
   evt = &cgs->events[cgs->events_used];
   init_Event(evt);
   evt->tag         = Ev_Bc;
   evt->inode       = inode;
   evt->Ev.Bc.taken = guard;
   cgs->events_used++;
}

static
IRSB* cv_instrument ( VgCallbackClosure* closure,
                      IRSB* sbIn, 
                      const VexGuestLayout* layout, 
                      const VexGuestExtents* vge,
                      const VexArchInfo* archinfo_host,
                      IRType gWordTy, IRType hWordTy )
{
   Int        i;
   UInt       isize;
   IRStmt*    st;
   Addr       cia; /* address of current insn */
   CgState    cgs;
   InstrInfo* curr_inode = NULL;
   DiEpoch    ep = VG_(current_DiEpoch)();
  

   if (gWordTy != hWordTy) {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   // Set up new SB
   cgs.sbOut = deepCopyIRSBExceptStmts(sbIn);

   // Copy verbatim any IR preamble preceding the first IMark
   i = 0;
   while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
      addStmtToIRSB( cgs.sbOut, sbIn->stmts[i] );
      i++;
   }

   // Get the first statement, and initial cia from it
   tl_assert(sbIn->stmts_used > 0);
   tl_assert(i < sbIn->stmts_used);
   st = sbIn->stmts[i];
   tl_assert(Ist_IMark == st->tag);

   cia   = st->Ist.IMark.addr;
   isize = st->Ist.IMark.len;
   // If Vex fails to decode an instruction, the size will be zero.
   // Pretend otherwise.
   if (isize == 0) isize = VG_MIN_INSTR_SZB;

   // Set up running state and get block info
   tl_assert(closure->readdr == vge->base[0]);
   cgs.events_used = 0;
   cgs.sbInfo      = get_SB_info(sbIn, (Addr)closure->readdr);
   cgs.sbInfo_i    = 0;


   // Traverse the block, initialising inodes, adding events and flushing as
   // necessary.
   for (/*use current i*/; i < sbIn->stmts_used; i++) {
      IRDirty*   di;
      IRExpr**     argv;
      IRExpr* expr;
    
      st = sbIn->stmts[i];
      tl_assert(isFlatIRStmt(st));

      switch (st->tag) {

         case Ist_IMark:
            cia   = st->Ist.IMark.addr;
            isize = st->Ist.IMark.len;

            expr = mkIRExpr_HWord( (HWord)cia);

            // If Vex fails to decode an instruction, the size will be zero.
            // Pretend otherwise.
            if (isize == 0) isize = VG_MIN_INSTR_SZB;

            // Sanity-check size.
            tl_assert( (VG_MIN_INSTR_SZB <= isize && isize <= VG_MAX_INSTR_SZB)
                     || VG_CLREQ_SZB == isize );

            // Get space for and init the inode, record it as the current one.
            // Subsequent Dr/Dw/Dm events from the same instruction will 
            // also use it.
            curr_inode = setup_InstrInfo(&cgs, cia, isize);

            addEvent_Ir( &cgs, curr_inode );
           
            if ((VG_(get_fnname_if_entry)(ep, st->Ist.IMark.addr, &fnname)
                  && (cia < 40000000))
                  && (0 != VG_(strcmp)(fnname, "_start")) 
                  && (0 != VG_(strcmp)(fnname, "__libc_csu_init"))) {

               if (!(entry*)VG_(HT_lookup)(func_calls_HT, cia)) {
                  insert_entry_HT(func_calls_HT, cia, fnname);
               }

               argv = mkIRExprVec_1(expr);
               tl_assert(argv);

               di = unsafeIRDirty_0_N(0, "update_count_HT", 
                                       VG_(fnptr_to_fnentry)( &update_count_HT), 
                                       argv);
                  addStmtToIRSB( cgs.sbOut, IRStmt_Dirty(di) );
               }

            break;

         case Ist_Exit: {
            // call branch predictor only if this is a branch in guest code
            if ( (st->Ist.Exit.jk == Ijk_Boring) ||
                 (st->Ist.Exit.jk == Ijk_Call) ||
                 (st->Ist.Exit.jk == Ijk_Ret) )
            {
               /* Stuff to widen the guard expression to a host word, so
                  we can pass it to the branch predictor simulation
                  functions easily. */
               Bool     inverted;
               Addr     nia, sea;
               IRConst* dst;
               IRType   tyW    = hWordTy;
               IROp     widen  = tyW==Ity_I32  ? Iop_1Uto32  : Iop_1Uto64;
               IROp     opXOR  = tyW==Ity_I32  ? Iop_Xor32   : Iop_Xor64;
               IRTemp   guard1 = newIRTemp(cgs.sbOut->tyenv, Ity_I1);
               IRTemp   guardW = newIRTemp(cgs.sbOut->tyenv, tyW);
               IRTemp   guard  = newIRTemp(cgs.sbOut->tyenv, tyW);
               IRExpr*  one    = tyW==Ity_I32 ? IRExpr_Const(IRConst_U32(1))
                                              : IRExpr_Const(IRConst_U64(1));

               /* First we need to figure out whether the side exit got
                  inverted by the ir optimiser.  To do that, figure out
                  the next (fallthrough) instruction's address and the
                  side exit address and see if they are the same. */
               nia = cia + isize;

               /* Side exit address */
               dst = st->Ist.Exit.dst;
               if (tyW == Ity_I32) {
                  tl_assert(dst->tag == Ico_U32);
                  sea = dst->Ico.U32;
               } else {
                  tl_assert(tyW == Ity_I64);
                  tl_assert(dst->tag == Ico_U64);
                  sea = dst->Ico.U64;
               }

               inverted = nia == sea;

               /* Widen the guard expression. */
               addStmtToIRSB( cgs.sbOut,
                              IRStmt_WrTmp( guard1, st->Ist.Exit.guard ));
               addStmtToIRSB( cgs.sbOut,
                              IRStmt_WrTmp( guardW,
                                            IRExpr_Unop(widen,
                                                        IRExpr_RdTmp(guard1))) );
               /* If the exit is inverted, invert the sense of the guard. */
               addStmtToIRSB(
                     cgs.sbOut,
                     IRStmt_WrTmp(
                           guard,
                           inverted ? IRExpr_Binop(opXOR, IRExpr_RdTmp(guardW), one)
                                    : IRExpr_RdTmp(guardW)
                              ));
               /* And post the event. */
               addEvent_Bc( &cgs, curr_inode, IRExpr_RdTmp(guard) );
            }

            /* We may never reach the next statement, so need to flush
               all outstanding transactions now. */
            flushEvents( &cgs );
            break;
         }

         default:
            break;
      }


      /* Copy the original statement */
      addStmtToIRSB( cgs.sbOut, st );

   }

   /* At the end of the bb.  Flush outstandings. */
   flushEvents( &cgs );

   /* done.  stay sane ... */
   tl_assert(cgs.sbInfo_i == cgs.sbInfo->n_instrs);

   // same_block = 0;

   return cgs.sbOut;
}


// Total reads/writes/misses.  Calculated during CC traversal at the end.
// All auto-zeroed.
// static ULong    Ex_total;
static ULong    Any_total;
static CacheCC  Ir_total;
static BranchCC Bc_total;

static void fprint_CC_table_and_calc_totals(void)
{
   Int     i;
   VgFile  *fp;
   HChar   *currFile = NULL;
   const HChar *currFn = NULL;
   LineCC* lineCC;

   src_dir_found = False;

   // Setup output filename.  Nb: it's important to do this now, ie. as late
   // as possible.  If we do it at start-up and the program forks and the
   // output file format string contains a %p (pid) specifier, both the
   // parent and child will incorrectly write to the same file;  this
   // happened in 3.3.0.
   HChar* covergrind_out_file =
      VG_(expand_file_name)("--covergrind-out-file", clo_covergrind_out_file);

   fp = VG_(fopen)(covergrind_out_file, VKI_O_CREAT|VKI_O_TRUNC|VKI_O_WRONLY,
                                        VKI_S_IRUSR|VKI_S_IWUSR);
   if (fp == NULL) {
      // If the file can't be opened for whatever reason (conflict
      // between multiple Covergrinded processes?), give up now.
      VG_(umsg)("error: can't open output file '%s'\n",
                covergrind_out_file );
      VG_(free)(covergrind_out_file);
      return;
   } else {
      VG_(free)(covergrind_out_file);
   }

   // "cmd:" line
   VG_(fprintf)(fp, "cmd: %s", VG_(args_the_exename));
   for (i = 0; i < VG_(sizeXA)( VG_(args_for_client) ); i++) {
      HChar* arg = * (HChar**) VG_(indexXA)( VG_(args_for_client), i );
      VG_(fprintf)(fp, " %s", arg);
   }

      VG_(fprintf)(fp, "\nevents: Ex Bc\n");
 

   // Traverse every lineCC
   VG_(OSetGen_ResetIter)(CC_table);
   while ( (lineCC = VG_(OSetGen_Next)(CC_table)) ) {

      Bool just_hit_a_new_file = False;
      // If we've hit a new file, print a "fl=" line.  Note that because
      // each string is stored exactly once in the string table, we can use
      // pointer comparison rather than strcmp() to test for equality, which
      // is good because most of the time the comparisons are equal and so
      // the whole strings would have to be checked.
      if ( lineCC->loc.file != currFile ) {
         currFile = lineCC->loc.file;
         VG_(fprintf)(fp, "fl=%s\n", currFile);
         just_hit_a_new_file = True;
      }

      // If we've hit a new function, print a "fn=" line.  We know to do
      // this when the function name changes, and also every time we hit a
      // new file (in which case the new function name might be the same as
      // in the old file, hence the just_hit_a_new_file test).
      if ( just_hit_a_new_file || lineCC->loc.fn != currFn ) {
         currFn = lineCC->loc.fn;
         VG_(fprintf)(fp, "fn=%s\n", currFn);
      }

      src_dir_found = False;

         VG_(fprintf)(fp,  "%d %llu"
                             " %llu\n",
                            lineCC->loc.line,
                            lineCC->instr_any,
                            lineCC->Bc.b);
    
      // Update summary stats
      Ir_total.a  += lineCC->Ir.a;
      Bc_total.b  += lineCC->Bc.b;
      Any_total += lineCC->instr_any;
   }
  
      VG_(fprintf)(fp,  "summary:"
                        " %llu"
                        " %llu\n", 
                        Any_total,
                        Bc_total.b);
   VG_(fclose)(fp);
}

static UInt ULong_width(ULong n)
{
   UInt w = 0;
   while (n > 0) {
      n = n / 10;
      w++;
   }
   if (w == 0) w = 1;
   return w + (w-1)/3;   // add space for commas
}

static void cv_fini(Int exitcode)
{
   static HChar fmt[128];   // OK; large enough

   Int l1;

   fprint_CC_table_and_calc_totals();

   if (VG_(clo_verbosity) == 0) 
      return;

   // Nb: this isn't called "MAX" because that overshadows a global on Darwin.
   #define cv_MAX(a, b)  ((a) >= (b) ? (a) : (b))

   /* I cache results.  Use the I_refs value to determine the first column
    * width. */
   l1 = ULong_width(Ir_total.a);
 
   /* Make format string, getting width right for numbers */
   VG_(sprintf)(fmt, "%%s %%,%dllu\n", l1);

   UInt num_nodes =  VG_(HT_count_nodes)(func_calls_HT);
   VG_(umsg)("Number of executed functions: %u\n", num_nodes);
   print_HT(func_calls_HT);
}


/*--------------------------------------------------------------------*/
/*--- Setup                                                        ---*/
/*--------------------------------------------------------------------*/

static void cv_post_clo_init(void); /* just below */

static void cv_pre_clo_init(void)
{
   VG_(details_name)            ("Covergrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("a code coverage tool");
   VG_(details_copyright_author)(
      "TBD");
   VG_(details_bug_reports_to)  (VG_BUGS_TO);
   VG_(details_avg_translation_sizeB) ( 500 );

   VG_(basic_tool_funcs)          (cv_post_clo_init,
                                   cv_instrument,
                                   cv_fini);

   func_calls_HT = VG_(HT_construct)("func_calls_HT");
   tl_assert(func_calls_HT);
}

static void cv_post_clo_init(void)
{

   CC_table =
      VG_(OSetGen_Create)(offsetof(LineCC, loc),
                          cmp_CodeLoc_LineCC,
                          VG_(malloc), "cg.main.cpci.1",
                          VG_(free));
   instrInfoTable =
      VG_(OSetGen_Create)(/*keyOff*/0,
                          NULL,
                          VG_(malloc), "cg.main.cpci.2",
                          VG_(free));
   stringTable =
      VG_(OSetGen_Create)(/*keyOff*/0,
                          stringCmp,
                          VG_(malloc), "cg.main.cpci.3",
                          VG_(free));
}

VG_DETERMINE_INTERFACE_VERSION(cv_pre_clo_init)

/*--------------------------------------------------------------------*/
/*--- end                                                          ---*/
/*--------------------------------------------------------------------*/

