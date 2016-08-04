#include "phase.h"
#include "pcode.h"

static void init_local_record( struct codegen* codegen,
   struct local_record* record );
static void push_local_record( struct codegen* codegen,
   struct local_record* record );
static void pop_local_record( struct codegen* codegen );
static void write_block_item( struct codegen* codegen, struct node* node );
static void write_assert( struct codegen* codegen, struct assert* assert );
static void write_runtime_assert( struct codegen* codegen,
   struct assert* assert );
static void visit_if( struct codegen* codegen, struct if_stmt* );
static void push_cond( struct codegen* codegen, struct cond* cond );
static void visit_switch( struct codegen* codegen, struct switch_stmt* );
static void write_switch_casegoto( struct codegen* codegen,
   struct switch_stmt* stmt );
static bool string_switch( struct switch_stmt* stmt );
static void write_switch( struct codegen* codegen, struct switch_stmt* stmt );
static void write_switch_cond( struct codegen* codegen,
   struct switch_stmt* stmt );
static void write_string_switch( struct codegen* codegen,
   struct switch_stmt* stmt );
static void visit_case( struct codegen* codegen, struct case_label* );
static void visit_while( struct codegen* codegen, struct while_stmt* );
static void write_while( struct codegen* codegen, struct while_stmt* stmt );
static void write_folded_while( struct codegen* codegen,
   struct while_stmt* stmt );
static bool while_stmt( struct while_stmt* stmt );
static void visit_for( struct codegen* codegen, struct for_stmt* );
static void visit_foreach( struct codegen* codegen,
   struct foreach_stmt* stmt );
static void init_foreach_writing( struct foreach_writing* writing );
static void foreach_array( struct codegen* codegen,
   struct foreach_writing* writing, struct foreach_stmt* stmt );
static void foreach_ref_array( struct codegen* codegen,
   struct foreach_writing* writing, struct foreach_stmt* stmt );
static void foreach_str( struct codegen* codegen,
   struct foreach_writing* writing, struct foreach_stmt* stmt );
static void visit_jump( struct codegen* codegen, struct jump* );
static void set_jumps_point( struct codegen* codegen, struct jump* jump,
   struct c_point* point );
static void visit_return( struct codegen* codegen, struct return_stmt* );
static void visit_paltrans( struct codegen* codegen, struct paltrans* );
static void visit_script_jump( struct codegen* codegen, struct script_jump* );
static void visit_label( struct codegen* codegen, struct label* );
static void visit_goto( struct codegen* codegen, struct goto_stmt* );
static void visit_expr_stmt( struct codegen* codegen, struct expr_stmt* stmt );

void c_write_block( struct codegen* codegen, struct block* stmt ) {
   struct local_record record;
   init_local_record( codegen, &record );
   push_local_record( codegen, &record );
   list_iter_t i;
   list_iter_init( &i, &stmt->stmts );
   while ( ! list_end( &i ) ) {
      write_block_item( codegen, list_data( &i ) );
      list_next( &i );
   }
   pop_local_record( codegen );
}

void init_local_record( struct codegen* codegen,
   struct local_record* record ) {
   record->parent = NULL;
   if ( codegen->local_record ) {
      record->index = codegen->local_record->index;
      record->func_size = codegen->local_record->func_size;
   }
   else {
      record->index = codegen->func->start_index;
      record->func_size = codegen->func->size;
   }
}

void push_local_record( struct codegen* codegen,
   struct local_record* record ) {
   record->parent = codegen->local_record;
   codegen->local_record = record;
}

void pop_local_record( struct codegen* codegen ) {
   codegen->local_record = codegen->local_record->parent;
}

void write_block_item( struct codegen* codegen, struct node* node ) {
   switch ( node->type ) {
   case NODE_VAR:
      c_visit_var( codegen, ( struct var* ) node );
      break;
   case NODE_CASE:
   case NODE_CASE_DEFAULT:
      visit_case( codegen, ( struct case_label* ) node );
      break;
   case NODE_GOTO_LABEL:
      visit_label( codegen, ( struct label* ) node );
      break;
   case NODE_ASSERT:
      write_assert( codegen,
         ( struct assert* ) node );
      break;
   case NODE_ENUMERATION:
   case NODE_TYPE_ALIAS:
   case NODE_FUNC:
      break;
   default:
      c_write_stmt( codegen, node );
      break;
   }
}

void write_assert( struct codegen* codegen, struct assert* assert ) {
   if ( ! assert->is_static && codegen->task->options->write_asserts ) {
      write_runtime_assert( codegen, assert );
   }
}

void write_runtime_assert( struct codegen* codegen, struct assert* assert ) {
   c_push_bool_expr( codegen, assert->cond );
   struct c_jump* exit_jump = c_create_jump( codegen, PCD_IFGOTO );
   c_append_node( codegen, &exit_jump->node );
   c_pcd( codegen, PCD_BEGINPRINT );
   // Print file.
   c_push_string( codegen, assert->file );
   c_pcd( codegen, PCD_PRINTSTRING );
   // Push line/column characters.
   c_pcd( codegen, PCD_PUSHNUMBER, ' ' );
   c_pcd( codegen, PCD_PUSHNUMBER, ':' );
   c_pcd( codegen, PCD_PUSHNUMBER, assert->pos.column );
   c_pcd( codegen, PCD_PUSHNUMBER, ':' );
   c_pcd( codegen, PCD_PUSHNUMBER, assert->pos.line );
   c_pcd( codegen, PCD_PUSHNUMBER, ':' );
   // Print line/column.
   c_pcd( codegen, PCD_PRINTCHARACTER );
   c_pcd( codegen, PCD_PRINTNUMBER );
   c_pcd( codegen, PCD_PRINTCHARACTER );
   c_pcd( codegen, PCD_PRINTNUMBER );
   c_pcd( codegen, PCD_PRINTCHARACTER );
   c_pcd( codegen, PCD_PRINTCHARACTER );
   // Print standard message prefix.
   c_push_string( codegen, codegen->assert_prefix );
   c_pcd( codegen, PCD_PRINTSTRING );
   // Print message.
   if ( assert->message ) {
      c_pcd( codegen, PCD_PUSHNUMBER, ' ' );
      c_pcd( codegen, PCD_PUSHNUMBER, ':' );
      c_pcd( codegen, PCD_PRINTCHARACTER );
      c_pcd( codegen, PCD_PRINTCHARACTER );
      c_push_string( codegen, assert->message );
      c_pcd( codegen, PCD_PRINTSTRING );
   }
   // Message properties, in ACS terms: HUDMSG_LOG, 0, CR_RED, 1.5, 0.5, 0.0
   c_pcd( codegen, PCD_MOREHUDMESSAGE );
   c_pcd( codegen, PCD_PUSHNUMBER, ( int ) 0x80000000 );
   c_pcd( codegen, PCD_PUSHNUMBER, 0 );
   c_pcd( codegen, PCD_PUSHNUMBER, 6 );
   c_pcd( codegen, PCD_PUSHNUMBER, 98304 );
   c_pcd( codegen, PCD_PUSHNUMBER, 16384 );
   c_pcd( codegen, PCD_PUSHNUMBER, 0 );
   c_pcd( codegen, PCD_ENDHUDMESSAGEBOLD );
   c_pcd( codegen, PCD_TERMINATE );
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   exit_jump->point = exit_point;
}

void c_write_stmt( struct codegen* codegen, struct node* node ) {
   switch ( node->type ) {
   case NODE_BLOCK:
      c_write_block( codegen, ( struct block* ) node );
      break;
   case NODE_IF:
      visit_if( codegen, ( struct if_stmt* ) node );
      break;
   case NODE_SWITCH:
      visit_switch( codegen, ( struct switch_stmt* ) node );
      break;
   case NODE_WHILE:
      visit_while( codegen, ( struct while_stmt* ) node );
      break;
   case NODE_FOR:
      visit_for( codegen, ( struct for_stmt* ) node );
      break;
   case NODE_FOREACH:
      visit_foreach( codegen,
         ( struct foreach_stmt* ) node );
      break;
   case NODE_JUMP:
      visit_jump( codegen, ( struct jump* ) node );
      break;
   case NODE_SCRIPT_JUMP:
      visit_script_jump( codegen, ( struct script_jump* ) node );
      break;
   case NODE_RETURN:
      visit_return( codegen, ( struct return_stmt* ) node );
      break;
   case NODE_GOTO:
      visit_goto( codegen, ( struct goto_stmt* ) node );
      break;
   case NODE_PALTRANS:
      visit_paltrans( codegen, ( struct paltrans* ) node );
      break;
   case NODE_EXPR_STMT:
      visit_expr_stmt( codegen,
         ( struct expr_stmt* ) node );
      break;
   case NODE_INLINE_ASM:
      p_visit_inline_asm( codegen,
         ( struct inline_asm* ) node );
      break;
   case NODE_STRUCTURE:
   case NODE_USING:
      break;
   default:
      UNREACHABLE();
   }
}

void visit_if( struct codegen* codegen, struct if_stmt* stmt ) {
   struct local_record record;
   init_local_record( codegen, &record );
   push_local_record( codegen, &record );
   push_cond( codegen, &stmt->cond );
   struct c_jump* else_jump = c_create_jump( codegen, PCD_IFNOTGOTO );
   struct c_jump* exit_jump = else_jump;
   c_append_node( codegen, &else_jump->node );
   c_write_stmt( codegen, stmt->body );
   if ( stmt->else_body ) {
      exit_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &exit_jump->node );
      struct c_point* else_point = c_create_point( codegen );
      c_append_node( codegen, &else_point->node );
      else_jump->point = else_point;
      c_write_stmt( codegen, stmt->else_body );
   }
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   exit_jump->point = exit_point;
   pop_local_record( codegen );
}

void push_cond( struct codegen* codegen, struct cond* cond ) {
   if ( cond->u.node->type == NODE_VAR ) {
      c_visit_var( codegen, cond->u.var );
      c_push_bool_cond_var( codegen, cond->u.var );
   }
   else {
      c_push_bool_expr( codegen, cond->u.expr );
   }
}

void visit_switch( struct codegen* codegen, struct switch_stmt* stmt ) {
   switch ( codegen->lang ) {
   case LANG_ACS:
      write_switch( codegen, stmt );
      break;
   case LANG_ACS95:
      write_switch_casegoto( codegen, stmt );
      break;
   case LANG_BCS:
      if ( string_switch( stmt ) ) {
         write_string_switch( codegen, stmt );
      }
      else {
         write_switch( codegen, stmt );
      }
   }
}

void write_switch_casegoto( struct codegen* codegen,
   struct switch_stmt* stmt ) {
   struct c_point* exit_point = c_create_point( codegen );
   // Condition.
   c_push_expr( codegen, stmt->cond.u.expr );
   // Case selection.
   bool zero_value_case = false;
   struct case_label* label = stmt->case_head;
   while ( label ) {
      label->point = c_create_point( codegen );
      struct c_casejump* jump = c_create_casejump( codegen,
         label->number->value, label->point );
      c_append_node( codegen, &jump->node );
      if ( label->number->value == 0 ) {
         zero_value_case = true;
      }
      label = label->next;
   }
   // Default case.
   struct c_point* default_point = exit_point;
   if ( stmt->case_default ) {
      default_point = c_create_point( codegen );
      stmt->case_default->point = default_point;
   }
   // Optimization: instead of using PCD_DROP and PCD_GOTO to jump to the
   // default point, use a single instruction to eat up the value and jump.
   if ( zero_value_case ) {
      struct c_jump* default_jump = c_create_jump( codegen, PCD_IFGOTO );
      c_append_node( codegen, &default_jump->node );
      default_jump->point = default_point;
   }
   else {
      c_pcd( codegen, PCD_DROP );
      struct c_jump* default_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &default_jump->node );
      default_jump->point = default_point;
   }
   // Body.
   c_write_stmt( codegen, stmt->body );
   c_append_node( codegen, &exit_point->node );
   set_jumps_point( codegen, stmt->jump_break, exit_point );
}

inline bool string_switch( struct switch_stmt* stmt ) {
   return ( stmt->cond.u.node->type == NODE_VAR ?
      stmt->cond.u.var->spec == SPEC_STR :
      stmt->cond.u.expr->spec == SPEC_STR );
}

void write_switch( struct codegen* codegen, struct switch_stmt* stmt ) {
   struct local_record record;
   init_local_record( codegen, &record );
   push_local_record( codegen, &record );
   struct c_point* exit_point = c_create_point( codegen );
   // Case selection.
   write_switch_cond( codegen, stmt );
   struct c_sortedcasejump* sorted_jump = c_create_sortedcasejump( codegen );
   c_append_node( codegen, &sorted_jump->node );
   struct case_label* label = stmt->case_head;
   while ( label ) {
      label->point = c_create_point( codegen );
      struct c_casejump* jump = c_create_casejump( codegen,
         label->number->value, label->point );
      c_append_casejump( sorted_jump, jump );
      label = label->next;
   }
   c_pcd( codegen, PCD_DROP );
   struct c_point* default_point = exit_point;
   if ( stmt->case_default ) {
      default_point = c_create_point( codegen );
      stmt->case_default->point = default_point;
   }
   struct c_jump* default_jump = c_create_jump( codegen, PCD_GOTO );
   c_append_node( codegen, &default_jump->node );
   default_jump->point = default_point;
   // Body.
   c_write_stmt( codegen, stmt->body );
   c_append_node( codegen, &exit_point->node );
   set_jumps_point( codegen, stmt->jump_break, exit_point );
   pop_local_record( codegen );
}

void write_switch_cond( struct codegen* codegen, struct switch_stmt* stmt ) {
   if ( stmt->cond.u.node->type == NODE_VAR ) {
      c_visit_var( codegen, stmt->cond.u.var );
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, stmt->cond.u.var->index );
   }
   else {
      c_push_expr( codegen, stmt->cond.u.expr );
   }
}

// NOTE: Right now, the implementation does a linear search when attempting to
// find the correct case. A binary search might be possible. 
void write_string_switch( struct codegen* codegen, struct switch_stmt* stmt ) {
   struct c_point* exit_point = c_create_point( codegen );
   // Case selection.
   write_switch_cond( codegen, stmt );
   struct case_label* label = stmt->case_head;
   while ( label ) {
      if ( label->next ) {
         c_pcd( codegen, PCD_DUP );
      }
      c_push_string( codegen, t_lookup_string( codegen->task,
         label->number->value ) );
      c_pcd( codegen, PCD_CALLFUNC, 2, EXTFUNC_STRCMP );
      struct c_jump* next_jump = c_create_jump( codegen, PCD_IFGOTO );
      c_append_node( codegen, &next_jump->node );
      if ( label->next ) {
         // Match.
         c_pcd( codegen, PCD_DROP );
         struct c_jump* jump = c_create_jump( codegen, PCD_GOTO );
         c_append_node( codegen, &jump->node );
         label->point = c_create_point( codegen );
         jump->point = label->point;
         // Jump to next case.
         struct c_point* next_point = c_create_point( codegen );
         c_append_node( codegen, &next_point->node );
         next_jump->point = next_point;
      }
      else {
         // On the last case, there is no need to drop the duplicated condition
         // string because it's not duplicated, so just go directly to the case
         // if a match is made.
         next_jump->opcode = PCD_IFNOTGOTO;
         label->point = c_create_point( codegen );
         next_jump->point = label->point;
      }
      label = label->next;
   }
   // The last case eats up the condition string. If no cases are present, the
   // string needs to be manually dropped.
   if ( ! stmt->case_head ) {
      c_pcd( codegen, PCD_DROP );
   }
   struct c_point* default_point = exit_point;
   if ( stmt->case_default ) {
      default_point = c_create_point( codegen );
      stmt->case_default->point = default_point;
   }
   struct c_jump* default_jump = c_create_jump( codegen, PCD_GOTO );
   c_append_node( codegen, &default_jump->node );
   default_jump->point = default_point;
   // Body.
   c_write_stmt( codegen, stmt->body );
   c_append_node( codegen, &exit_point->node );
   set_jumps_point( codegen, stmt->jump_break, exit_point );
}

void visit_case( struct codegen* codegen, struct case_label* label ) {
   c_append_node( codegen, &label->point->node );
}

void visit_while( struct codegen* codegen, struct while_stmt* stmt ) {
   if ( stmt->cond.u.node->type == NODE_EXPR && stmt->cond.u.expr->folded ) {
      write_folded_while( codegen, stmt );
   }
   else {
      write_while( codegen, stmt );
   }
}

void write_while( struct codegen* codegen, struct while_stmt* stmt ) {
   struct local_record record;
   init_local_record( codegen, &record );
   push_local_record( codegen, &record );
   bool while_until = ( stmt->type == WHILE_WHILE ||
      stmt->type == WHILE_UNTIL );
   // Initial jump to condition for while/until loop.
   struct c_jump* cond_jump = NULL;
   if ( while_until ) {
      cond_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &cond_jump->node );
   }
   // Body.
   struct c_point* body_point = c_create_point( codegen );
   c_append_node( codegen, &body_point->node );
   c_write_stmt( codegen, stmt->body );
   // Condition.
   struct c_point* cond_point = c_create_point( codegen );
   c_append_node( codegen, &cond_point->node );
   push_cond( codegen, &stmt->cond );
   if ( while_until ) {
      cond_jump->point = cond_point;
   }
   struct c_jump* body_jump = c_create_jump( codegen,
      ( while_stmt( stmt ) ? PCD_IFGOTO : PCD_IFNOTGOTO ) );
   c_append_node( codegen, &body_jump->node );
   body_jump->point = body_point;
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   set_jumps_point( codegen, stmt->jump_break, exit_point );
   set_jumps_point( codegen, stmt->jump_continue, cond_point );
   pop_local_record( codegen );
}

void write_folded_while( struct codegen* codegen, struct while_stmt* stmt ) {
   bool true_cond = ( while_stmt( stmt ) && stmt->cond.u.expr->value != 0 ) ||
      ( stmt->cond.u.expr->value == 0 );
   // A loop with a constant false condition never executes, so jump to the
   // exit right away. Nonetheless, the loop is still written because it can be
   // entered with a goto-statement.
   struct c_jump* exit_jump = NULL;
   if ( ! true_cond ) {
      exit_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &exit_jump->node );
   }
   // Body.
   struct c_point* body_point = c_create_point( codegen );
   c_append_node( codegen, &body_point->node );
   c_write_stmt( codegen, stmt->body );
   // Jump to top of body. With a false condition, the loop will never
   // execute and so this jump is not needed.
   if ( true_cond ) {
      struct c_jump* body_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &body_jump->node );
      body_jump->point = body_point;
   }
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   if ( ! true_cond ) {
      exit_jump->point = exit_point;
   }
   set_jumps_point( codegen, stmt->jump_break, exit_point );
   set_jumps_point( codegen, stmt->jump_continue,
      ( true_cond ) ? body_point : exit_point );
}

bool while_stmt( struct while_stmt* stmt ) {
   return ( stmt->type == WHILE_WHILE || stmt->type == WHILE_DO_WHILE );
}

// for-loop layout:
// <initialization>
// <goto-condition>
// <body>
// <post-expression-list>
// <condition>
//   if 1: <goto-body>
//
// for-loop layout (constant-condition, 1):
// <initialization>
// <body>
// <post-expression-list>
// <goto-body>
//
// for-loop layout (constant-condition, 0):
// <initialization>
// <goto-done>
// <body>
// <post-expression-list>
// <done>
void visit_for( struct codegen* codegen, struct for_stmt* stmt ) {
   // Initialization.
   struct local_record record;
   init_local_record( codegen, &record );
   push_local_record( codegen, &record );
   list_iter_t i;
   list_iter_init( &i, &stmt->init );
   while ( ! list_end( &i ) ) {
      struct node* node = list_data( &i );
      switch ( node->type ) {
      case NODE_EXPR:
         c_visit_expr( codegen,
            ( struct expr* ) node );
         break;
      case NODE_VAR:
         c_visit_var( codegen,
            ( struct var* ) node );
         break;
      default:
         break;
      }
      list_next( &i );
   }
   // Only test a condition that isn't both constant and true.
   bool test_cond = false;
   if ( stmt->cond.u.node ) {
      if ( stmt->cond.u.node->type == NODE_VAR ) {
         test_cond = true;
      }
      else {
         test_cond = ( ! ( stmt->cond.u.expr->folded &&
            stmt->cond.u.expr->value != 0 ) );
      }
   }
   // Jump to condition.
   struct c_jump* cond_jump = NULL;
   if ( test_cond ) {
      cond_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &cond_jump->node );
   }
   // Body.
   struct c_point* body_point = c_create_point( codegen );
   c_append_node( codegen, &body_point->node );
   c_write_stmt( codegen, stmt->body );
   // Post expressions.
   struct c_point* post_point = NULL;
   if ( list_size( &stmt->post ) ) {
      post_point = c_create_point( codegen );
      c_append_node( codegen, &post_point->node );
      list_iter_init( &i, &stmt->post );
      while ( ! list_end( &i ) ) {
         struct node* node = list_data( &i );
         if ( node->type == NODE_EXPR ) {
            c_visit_expr( codegen, ( struct expr* ) node );
         }
         list_next( &i );
      }
   }
   // Condition.
   struct c_point* cond_point = NULL;
   if ( test_cond ) {
      cond_point = c_create_point( codegen );
      c_append_node( codegen, &cond_point->node );
      push_cond( codegen, &stmt->cond );
      cond_jump->point = cond_point;
   }
   // Jump to body.
   struct c_jump* body_jump = c_create_jump( codegen,
      ( test_cond ) ? PCD_IFGOTO : PCD_GOTO );
   c_append_node( codegen, &body_jump->node );
   body_jump->point = body_point;
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   // Patch jump statements.
   set_jumps_point( codegen, stmt->jump_break, exit_point );
   set_jumps_point( codegen, stmt->jump_continue,
      post_point ? post_point :
      cond_point ? cond_point :
      body_point );
   pop_local_record( codegen );
}

void visit_foreach( struct codegen* codegen, struct foreach_stmt* stmt ) {
   struct local_record record;
   init_local_record( codegen, &record );
   push_local_record( codegen, &record );
   struct foreach_writing writing;
   init_foreach_writing( &writing );
   c_push_foreach_collection( codegen, &writing, stmt->collection );
   switch ( writing.collection ) {
   case FOREACHCOLLECTION_ARRAY:
      foreach_array( codegen, &writing, stmt );
      break;
   case FOREACHCOLLECTION_ARRAYREF:
      foreach_ref_array( codegen, &writing, stmt );
      break;
   case FOREACHCOLLECTION_STR:
      foreach_str( codegen, &writing, stmt );
      break;
   }
   // Patch jump statements.
   set_jumps_point( codegen, stmt->jump_break, writing.break_point );
   set_jumps_point( codegen, stmt->jump_continue, writing.continue_point );
   pop_local_record( codegen );
}

void init_foreach_writing( struct foreach_writing* writing ) {
   writing->structure = NULL;
   writing->dim = NULL;
   writing->break_point = NULL;
   writing->continue_point = NULL;
   writing->storage = STORAGE_LOCAL;
   writing->index = 0;
   writing->diminfo = 0;
   writing->collection = FOREACHCOLLECTION_STR;
   writing->item = FOREACHITEM_PRIMITIVE;
   writing->pushed_base = false;
}

void foreach_array( struct codegen* codegen, struct foreach_writing* writing,
   struct foreach_stmt* stmt ) {
   // Allocate key variable.
   int key = c_alloc_script_var( codegen );
   c_pcd( codegen, PCD_PUSHNUMBER, 0 );
   c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, key );
   if ( stmt->key ) {
      stmt->key->index = key;
   }
   // Allocate value variable.
   c_visit_var( codegen, stmt->value );
   int value = stmt->value->index;
   // When iterating over sub-arrays, the dimension information will be the
   // same for each sub-array, so assign it only once.
   if ( writing->item == FOREACHITEM_SUBARRAY ) {
      c_pcd( codegen, PCD_PUSHNUMBER, writing->diminfo + 1 );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value + 1 );
   }
   // Allocate element-offset variable.
   // -----------------------------------------------------------------------
   // When the item is a sub-array or a structure, we use a single variable to
   // hold both the value and the element offset, since both values will be the
   // same.
   int offset = value;
   if ( ! ( writing->item == FOREACHITEM_SUBARRAY ||
      writing->item == FOREACHITEM_STRUCT ) ) {
      // When the item is of size 1, we use a single variable to hold both the
      // key and the element offset, since both values will be the same.
      offset = key;
      if ( writing->dim->element_size > 1 || writing->pushed_base ) {
         offset = c_alloc_script_var( codegen );
      }
   }
   if ( offset != key ) {
      if ( ! writing->pushed_base ) {
         c_pcd( codegen, PCD_PUSHNUMBER, 0 );
      }
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, offset );
   }
   // Beginning of loop.
   struct c_point* start_point = c_create_point( codegen );
   c_append_node( codegen, &start_point->node );
   // Initialize value variable.
   switch ( writing->item ) {
   case FOREACHITEM_ARRAYREF:
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, offset );
      c_push_element( codegen, writing->storage, writing->index );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value );
      c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, offset );
      c_push_element( codegen, writing->storage, writing->index );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value + 1 );
      break;
   case FOREACHITEM_PRIMITIVE:
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, offset );
      c_push_element( codegen, writing->storage, writing->index );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value );
      break;
   case FOREACHITEM_SUBARRAY:
   case FOREACHITEM_STRUCT:
      break;
   }
   // Body.
   c_write_stmt( codegen, stmt->body );
   // Increment.
   struct c_point* increment_point = c_create_point( codegen );
   c_append_node( codegen, &increment_point->node );
   writing->continue_point = increment_point;
   c_pcd( codegen, PCD_INCSCRIPTVAR, key );
   if ( offset != key ) {
      // The offset variable is incremented once to get the dimension
      // information. Increment again to move on to the next element.
      if ( writing->item == FOREACHITEM_ARRAYREF ) {
         c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      }
      else if ( writing->dim->element_size > 1 ) {
         c_pcd( codegen, PCD_PUSHNUMBER, writing->dim->element_size );
         c_pcd( codegen, PCD_ADDSCRIPTVAR, offset );
      }
      else {
         c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      }
   }
   // Condition.
   c_pcd( codegen, PCD_PUSHSCRIPTVAR, key );
   c_pcd( codegen, PCD_PUSHNUMBER, writing->dim->length );
   c_pcd( codegen, PCD_LT );
   struct c_jump* start_jump = c_create_jump( codegen, PCD_IFGOTO );
   c_append_node( codegen, &start_jump->node );
   start_jump->point = start_point;
   // Exit.
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   writing->break_point = exit_point;
}

void foreach_ref_array( struct codegen* codegen,
   struct foreach_writing* writing, struct foreach_stmt* stmt ) {
   // Allocate key variable.
   int key = -1;
   if ( stmt->key ) {
      key = c_alloc_script_var( codegen );
      c_pcd( codegen, PCD_PUSHNUMBER, 0 );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, key );
      stmt->key->index = key;
   }
   // Allocate value variable.
   c_visit_var( codegen, stmt->value );
   int value = stmt->value->index;
   // When iterating over sub-arrays, the dimension information will be the
   // same for each sub-array, so assign it only once.
   if ( writing->item == FOREACHITEM_SUBARRAY ) {
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value + 1 );
   }
   // Allocate elements-left variable.
   int left = c_alloc_script_var( codegen );
   c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, left );
   // Allocate element-offset variable.
   // -----------------------------------------------------------------------
   // When the item is a sub-array or a structure, we use a single variable to
   // hold both the value and the element offset, since both values will be the
   // same.
   int offset = value;
   if ( ! ( writing->item == FOREACHITEM_SUBARRAY ||
      writing->item == FOREACHITEM_STRUCT ) ) {
      offset = c_alloc_script_var( codegen );
   }
   c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, offset );
   // Beginning of loop.
   struct c_point* start_point = c_create_point( codegen );
   c_append_node( codegen, &start_point->node );
   // Initialize value variable.
   switch ( writing->item ) {
   case FOREACHITEM_ARRAYREF:
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, offset );
      c_push_element( codegen, writing->storage, writing->index );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value );
      c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, offset );
      c_push_element( codegen, writing->storage, writing->index );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value + 1 );
      break;
   case FOREACHITEM_PRIMITIVE:
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, offset );
      c_push_element( codegen, writing->storage, writing->index );
      c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value );
      break;
   case FOREACHITEM_SUBARRAY:
   case FOREACHITEM_STRUCT:
      break;
   }
   // Body.
   c_write_stmt( codegen, stmt->body );
   // Increment.
   struct c_point* increment_point = c_create_point( codegen );
   c_append_node( codegen, &increment_point->node );
   writing->continue_point = increment_point;
   if ( key >= 0 ) {
      c_pcd( codegen, PCD_INCSCRIPTVAR, key );
   }
   switch ( writing->item ) {
   case FOREACHITEM_SUBARRAY:
      // Because the iteration variable containing the offset to the dimension
      // information is assigned only once and remains unchanged, use it to get
      // the sub-array size, which is the increment amount.
      c_pcd( codegen, PCD_PUSHSCRIPTVAR, value + 1 );
      c_pcd( codegen, PCD_PUSHMAPARRAY, codegen->shary.index );
      c_pcd( codegen, PCD_ADDSCRIPTVAR, offset );
      break;
   case FOREACHITEM_ARRAYREF:
      // The offset variable is incremented once to get the dimension
      // information. Increment again to move on to the next element.
      c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      break;
   case FOREACHITEM_STRUCT:
      if ( writing->structure->size > 1 ) {
         c_pcd( codegen, PCD_PUSHNUMBER, writing->structure->size );
         c_pcd( codegen, PCD_ADDSCRIPTVAR, offset );
      }
      else {
         c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      }
      break;
   case FOREACHITEM_PRIMITIVE:
      c_pcd( codegen, PCD_INCSCRIPTVAR, offset );
      break;
   }
   // Condition.
   c_pcd( codegen, PCD_DECSCRIPTVAR, left );
   c_pcd( codegen, PCD_PUSHSCRIPTVAR, left );
   struct c_jump* start_jump = c_create_jump( codegen, PCD_IFGOTO );
   c_append_node( codegen, &start_jump->node );
   start_jump->point = start_point;
   // Exit.
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   writing->break_point = exit_point;
}

void foreach_str( struct codegen* codegen, struct foreach_writing* writing,
   struct foreach_stmt* stmt ) {
   // Allocate string variable.
   int string = c_alloc_script_var( codegen );
   c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, string );
   // Allocate key variable.
   int key = c_alloc_script_var( codegen );
   c_pcd( codegen, PCD_PUSHNUMBER, 0 );
   c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, key );
   if ( stmt->key ) {
      stmt->key->index = key;
   }
   // Allocate value variable.
   c_visit_var( codegen, stmt->value );
   int value = stmt->value->index;
   struct c_jump* cond_jump = c_create_jump( codegen, PCD_GOTO );
   c_append_node( codegen, &cond_jump->node );
   // Beginning of loop.
   struct c_point* start_point = c_create_point( codegen );
   c_append_node( codegen, &start_point->node );
   // Body.
   c_write_stmt( codegen, stmt->body );
   // Increment.
   c_pcd( codegen, PCD_INCSCRIPTVAR, key );
   // Condition.
   struct c_point* cond_point = c_create_point( codegen );
   c_append_node( codegen, &cond_point->node );
   cond_jump->point = cond_point;
   writing->continue_point = cond_point;
   c_pcd( codegen, PCD_PUSHSCRIPTVAR, string );
   c_pcd( codegen, PCD_PUSHSCRIPTVAR, key );
   c_pcd( codegen, PCD_CALLFUNC, 2, EXTFUNC_GETCHAR );
   c_pcd( codegen, PCD_DUP );
   c_pcd( codegen, PCD_ASSIGNSCRIPTVAR, value );
   struct c_jump* start_jump = c_create_jump( codegen, PCD_IFGOTO );
   c_append_node( codegen, &start_jump->node );
   start_jump->point = start_point;
   // Exit.
   struct c_point* exit_point = c_create_point( codegen );
   c_append_node( codegen, &exit_point->node );
   writing->break_point = exit_point;
}

void visit_jump( struct codegen* codegen, struct jump* jump ) {
   struct c_jump* point_jump = c_create_jump( codegen, PCD_GOTO );
   c_append_node( codegen, &point_jump->node );
   jump->point_jump = point_jump;
}

void set_jumps_point( struct codegen* codegen, struct jump* jump,
   struct c_point* point ) {
   while ( jump ) {
      jump->point_jump->point = point;
      jump = jump->next;
   }
}

void visit_return( struct codegen* codegen, struct return_stmt* stmt ) {
   if ( codegen->func->nested_func ) {
      if ( stmt->return_value ) {
         c_push_initz_expr( codegen, codegen->func->func->ref,
            stmt->return_value->expr );
      }
      struct c_jump* epilogue_jump = c_create_jump( codegen, PCD_GOTO );
      c_append_node( codegen, &epilogue_jump->node );
      stmt->epilogue_jump = epilogue_jump;
   }
   else {
      if ( stmt->return_value ) {
         c_push_initz_expr( codegen, codegen->func->func->ref,
            stmt->return_value->expr );
         c_pcd( codegen, PCD_RETURNVAL );
      }
      else {
         c_pcd( codegen, PCD_RETURNVOID );
      }
   }
}

void visit_paltrans( struct codegen* codegen, struct paltrans* trans ) {
   c_push_expr( codegen, trans->number );
   c_pcd( codegen, PCD_STARTTRANSLATION );
   struct palrange* range = trans->ranges;
   while ( range ) {
      c_push_expr( codegen, range->begin );
      c_push_expr( codegen, range->end );
      if ( range->rgb ) {
         c_push_expr( codegen, range->value.rgb.red1 );
         c_push_expr( codegen, range->value.rgb.green1 );
         c_push_expr( codegen, range->value.rgb.blue1 );
         c_push_expr( codegen, range->value.rgb.red2 );
         c_push_expr( codegen, range->value.rgb.green2 );
         c_push_expr( codegen, range->value.rgb.blue2 );
         c_pcd( codegen, PCD_TRANSLATIONRANGE2 );
      }
      else {
         c_push_expr( codegen, range->value.ent.begin );
         c_push_expr( codegen, range->value.ent.end );
         c_pcd( codegen, PCD_TRANSLATIONRANGE1 );
      }
      range = range->next;
   }
   c_pcd( codegen, PCD_ENDTRANSLATION );
}

void visit_script_jump( struct codegen* codegen, struct script_jump* stmt ) {
   switch ( stmt->type ) {
   case SCRIPT_JUMP_SUSPEND:
      c_pcd( codegen, PCD_SUSPEND );
      break;
   case SCRIPT_JUMP_RESTART:
      c_pcd( codegen, PCD_RESTART );
      break;
   default:
      c_pcd( codegen, PCD_TERMINATE );
      break;
   }
}

void visit_label( struct codegen* codegen, struct label* label ) {
   if ( ! label->point ) {
      label->point = c_create_point( codegen );
   }
   c_append_node( codegen, &label->point->node );
}

void visit_goto( struct codegen* codegen, struct goto_stmt* stmt ) {
   struct c_jump* jump = c_create_jump( codegen, PCD_GOTO );
   c_append_node( codegen, &jump->node );
   if ( ! stmt->label->point ) {
      stmt->label->point = c_create_point( codegen );
   }
   jump->point = stmt->label->point;
}

void visit_expr_stmt( struct codegen* codegen, struct expr_stmt* stmt ) {
   list_iter_t i;
   list_iter_init( &i, &stmt->expr_list );
   while ( ! list_end( &i ) ) {
      c_visit_expr( codegen, list_data( &i ) );
      list_next( &i );
   }
}