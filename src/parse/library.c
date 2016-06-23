#include <string.h>

#include "phase.h"
#include "cache/cache.h"

enum pseudo_dirc {
   PSEUDODIRC_UNKNOWN,
   PSEUDODIRC_DEFINE,
   PSEUDODIRC_LIBDEFINE,
   PSEUDODIRC_INCLUDE,
   PSEUDODIRC_IMPORT,
   PSEUDODIRC_LIBRARY,
   PSEUDODIRC_ENCRYPTSTRINGS,
   PSEUDODIRC_WADAUTHOR,
   PSEUDODIRC_NOWADAUTHOR,
   PSEUDODIRC_NOCOMPACT,
   PSEUDODIRC_REGION,
   PSEUDODIRC_ENDREGION,
   PSEUDODIRC_MNEMONIC
};

static void read_module( struct parse* parse );
static void read_module_item( struct parse* parse );
static void read_module_item_acs( struct parse* parse );
static bool peek_header_namespace( struct parse* parse );
static void read_namespace( struct parse* parse, bool read_header_ns );
static void read_namespace_name( struct parse* parse, struct ns* parent_ns );
static void read_namespace_member_list( struct parse* parse );
static void read_namespace_member( struct parse* parse );
static struct using_dirc* alloc_using( struct pos* pos );
static void read_using_item( struct parse* parse, struct using_dirc* dirc );
static struct using_item* alloc_using_item( void );
static struct path* alloc_path( struct pos pos );
static void read_pseudo_dirc( struct parse* parse, bool first_object );
static enum tk determine_bcs_pseudo_dirc( struct parse* parse );
static void read_include( struct parse* parse );
static void read_define( struct parse* parse );
static void read_import( struct parse* parse, struct pos* pos );
static void read_library( struct parse* parse, struct pos* pos,
   bool first_object );
static void read_library_name( struct parse* parse, struct pos* pos );
static void read_imported_libs( struct parse* parse );
static void import_lib( struct parse* parse, struct import_dirc* dirc );
static struct library* get_previously_processed_lib( struct parse* parse,
   struct file_entry* file );
static struct library* find_lib( struct parse* parse,
   struct file_entry* file );
static void append_imported_lib( struct parse* parse, struct library* lib );

void p_read_target_lib( struct parse* parse ) {
   read_module( parse );
   read_imported_libs( parse );
}

void read_module( struct parse* parse ) {
   if ( parse->tk == TK_HASH ) {
      read_pseudo_dirc( parse, true ); 
   }
   while ( parse->tk != TK_END ) {
      read_module_item( parse );
   }
   if ( parse->lang == LANG_BCS ) {
      // Enable strong mode.
      if ( p_find_macro( parse, "__strongmode" ) ) {
         parse->lib->type_mode = TYPEMODE_STRONG;
      }
   }
}

void read_module_item( struct parse* parse ) {
   switch ( parse->lang ) {
   case LANG_ACS:
   case LANG_ACS95:
      read_module_item_acs( parse );
      break;
   default:
      switch ( parse->tk ) {
      case TK_SPECIAL:
         p_read_special_list( parse );
         break;
      case TK_HASH:
         read_pseudo_dirc( parse, false );
         break;
      case TK_NAMESPACE:
         read_namespace( parse, peek_header_namespace( parse ) );
         break;
      default:
         read_namespace_member( parse );
         break;
      }
      break;
   }
}

void read_module_item_acs( struct parse* parse ) {
   if ( p_is_dec( parse ) || parse->tk == TK_FUNCTION ) {
      struct dec dec;
      p_init_dec( &dec );
      dec.name_offset = parse->ns->body;
      p_read_dec( parse, &dec );
   }
   else {
      switch ( parse->tk ) {
      case TK_SCRIPT:
         p_read_script( parse );
         break;
      case TK_HASH:
         read_pseudo_dirc( parse, false );
         break;
      case TK_SPECIAL:
         p_read_special_list( parse );
         break;
      default:
         p_diag( parse, DIAG_POS_ERR, &parse->tk_pos,
            "unexpected %s", p_get_token_name( parse->tk ) );
         p_bail( parse );
      }
   }
}

bool peek_header_namespace( struct parse* parse ) {
   struct parsertk_iter iter;
   p_init_parsertk_iter( parse, &iter );
   p_next_tk( parse, &iter );
   bool match = ( iter.token->type == TK_ID );
   p_next_tk( parse, &iter );
   while ( match && iter.token->type == TK_DOT ) {
      p_next_tk( parse, &iter );
      match = ( iter.token->type == TK_ID );
      p_next_tk( parse, &iter );
   }
   match = ( match && iter.token->type == TK_SEMICOLON );
   return match;
}

void read_namespace( struct parse* parse, bool read_header_ns ) {
   struct pos pos = parse->tk_pos;
   p_test_tk( parse, TK_NAMESPACE );
   p_read_tk( parse );
   struct ns* parent = parse->ns;
   read_namespace_name( parse, ( read_header_ns ) ? parse->lib->upmost_ns :
      parse->ns );
   // A namespace can be opened only once. This behaves like modules in other
   // languages.
   if ( parse->ns->defined ) {
      p_diag( parse, DIAG_POS_ERR, &pos,
         "duplicate namespace" );
      p_diag( parse, DIAG_POS, &parse->ns->object.pos,
         "namespace already defined here" );
      p_bail( parse );
   }
   parse->ns->object.pos = pos;
   parse->ns->defined = true;
   if ( read_header_ns ) {
      p_test_tk( parse, TK_SEMICOLON );
      p_read_tk( parse );
      parse->ns->explicit_imports = true;
   }
   else {
      p_test_tk( parse, TK_BRACE_L );
      p_read_tk( parse );
      read_namespace_member_list( parse );
      p_test_tk( parse, TK_BRACE_R );
      p_read_tk( parse );
      parse->ns = parent;
   }
}

void read_namespace_name( struct parse* parse, struct ns* parent_ns ) {
   while ( true ) {
      p_test_tk( parse, TK_ID );
      struct name* name = t_extend_name( parent_ns->body, parse->tk_text );
      if ( ! name->object ) {
         struct ns* ns = t_alloc_ns( parse->task, name );
         list_append( &parse->lib->namespaces, ns );
         t_append_unresolved_namespace_object( parent_ns, &ns->object );
         list_append( &parent_ns->objects, ns );
         ns->parent = parent_ns;
         name->object = &ns->object;
      }
      parse->ns = ( struct ns* ) name->object;
      p_read_tk( parse );
      if ( parse->tk == TK_DOT ) {
         p_read_tk( parse );
      }
      else {
         break;
      }
      parent_ns = parse->ns;
   }
}

void read_namespace_member_list( struct parse* parse ) {
   while ( parse->tk != TK_BRACE_R ) {
      read_namespace_member( parse );
   }
}

void read_namespace_member( struct parse* parse ) {
   if ( p_is_dec( parse ) ) {
      struct dec dec;
      p_init_dec( &dec );
      dec.name_offset = parse->ns->body;
      p_read_dec( parse, &dec );
   }
   else if ( parse->tk == TK_SCRIPT ) {
      if ( ! parse->lib->imported ) {
         p_read_script( parse );
      }
      else {
         p_skip_block( parse );
      }
   }
   else if ( parse->tk == TK_NAMESPACE ) {
      read_namespace( parse, false );
   }
   else if ( parse->tk == TK_USING ) {
      p_read_using( parse, &parse->ns->usings );
   }
   else if ( parse->tk == TK_SEMICOLON ) {
      p_read_tk( parse );
   }
   else {
      p_diag( parse, DIAG_POS_ERR, &parse->tk_pos,
         "unexpected %s", p_get_token_name( parse->tk ) );
      p_bail( parse );
   }
}

void p_read_using( struct parse* parse, struct list* output ) {
   struct using_dirc* dirc = alloc_using( &parse->tk_pos );
   p_test_tk( parse, TK_USING );
   p_read_tk( parse );
   dirc->path = p_read_path( parse );
   if ( parse->tk == TK_COLON ) {
      p_read_tk( parse );
      while ( true ) {
         read_using_item( parse, dirc );
         if ( parse->tk == TK_COMMA ) {
            p_read_tk( parse );
         }
         else {
            break;
         }
      }
      dirc->type = USING_SELECTION;
   }
   p_test_tk( parse, TK_SEMICOLON );
   p_read_tk( parse );
   list_append( output, dirc );
}

struct using_dirc* alloc_using( struct pos* pos ) {
   struct using_dirc* dirc = mem_alloc( sizeof( *dirc ) );
   dirc->node.type = NODE_USING;
   dirc->path = NULL;
   list_init( &dirc->items );
   dirc->pos = *pos;
   dirc->type = USING_ALL;
   return dirc;
}

void read_using_item( struct parse* parse, struct using_dirc* dirc ) {
   p_test_tk( parse, TK_ID );
   struct using_item* item = alloc_using_item();
   item->name = parse->tk_text;
   item->imported_object = NULL;
   item->pos = parse->tk_pos;
   list_append( &dirc->items, item );
   p_read_tk( parse );
}

struct using_item* alloc_using_item( void ) {
   struct using_item* item = mem_alloc( sizeof( *item ) );
   item->name = NULL;
   return item;
}

struct path* p_read_path( struct parse* parse ) {
   // Head of path.
   struct path* path = alloc_path( parse->tk_pos );
   if ( parse->tk == TK_UPMOST ) {
      path->upmost = true;
      p_read_tk( parse );
   }
   else {
      p_test_tk( parse, TK_ID );
      path->text = parse->tk_text;
      p_read_tk( parse );
   }
   // Tail of path.
   struct path* head = path;
   struct path* tail = head;
   while ( parse->tk == TK_DOT && p_peek( parse ) == TK_ID ) {
      p_read_tk( parse );
      p_test_tk( parse, TK_ID );
      path = alloc_path( parse->tk_pos );
      path->text = parse->tk_text;
      tail->next = path;
      tail = path;
      p_read_tk( parse );
   }
   return head;
}

struct path* alloc_path( struct pos pos ) {
   struct path* path = mem_alloc( sizeof( *path ) );
   path->next = NULL;
   path->text = NULL;
   path->pos = pos;
   path->upmost = false;
   return path;
}

bool p_peek_path( struct parse* parse, struct parsertk_iter* iter ) {
/*
   switch ( iter->token->type ) {
   case TK_UPMOST:
   case TK_ID:
      p_next_tk( parse, iter );
      break;
   default:
      return false;
   }
*/
   while ( iter->token->type == TK_DOT ) {
      p_next_tk( parse, iter );
      if ( iter->token->type != TK_ID ) {
         return false;
      }
      p_next_tk( parse, iter );
   }
   return true;
}

void read_pseudo_dirc( struct parse* parse, bool first_object ) {
   struct pos pos = parse->tk_pos;
   p_test_tk( parse, TK_HASH );
   p_read_tk( parse );
   // Determine the directive to read.
   enum tk dirc = TK_NONE;
   switch ( parse->lang ) {
   case LANG_ACS:
      switch ( parse->tk ) {
      case TK_DEFINE:
      case TK_LIBDEFINE:
      case TK_INCLUDE:
      case TK_IMPORT:
      case TK_LIBRARY:
      case TK_ENCRYPTSTRINGS:
      case TK_NOCOMPACT:
      case TK_WADAUTHOR:
      case TK_NOWADAUTHOR:
      case TK_REGION:
      case TK_ENDREGION:
         dirc = parse->tk;
         break;
      default:
         break;
      }
      break;
   case LANG_ACS95:
      switch ( parse->tk ) {
      case TK_DEFINE:
      case TK_INCLUDE:
         dirc = parse->tk;
         break;
      default:
         break;
      }
      break;
   default:
      dirc = determine_bcs_pseudo_dirc( parse );
   }
   // Read directive.
   switch ( dirc ) {
   case TK_DEFINE:
   case TK_LIBDEFINE:
      read_define( parse );
      break;
   case TK_INCLUDE:
      read_include( parse );
      break;
   case TK_IMPORT:
      read_import( parse, &pos );
      break;
   case TK_LIBRARY:
      read_library( parse, &pos, first_object );
      break;
   case TK_ENCRYPTSTRINGS:
      parse->lib->encrypt_str = true;
      p_read_tk( parse );
      break;
   case TK_NOCOMPACT:
      // NOTE: This restriction doesn't apply to our compiler, but keep it to
      // stay compatible with acc. 
      if ( parse->lang == LANG_ACS && (
         list_size( &parse->lib->scripts ) > 0 ||
         list_size( &parse->lib->funcs ) > 0 ) ) {
         p_diag( parse, DIAG_POS_ERR, &pos,
            "`%s` directive found after a script or a function",
            parse->tk_text );
         p_bail( parse );
      }
      parse->lib->format = FORMAT_BIG_E;
      p_read_tk( parse );
      break;
   case TK_WADAUTHOR:
   case TK_NOWADAUTHOR:
      parse->lib->wadauthor = ( dirc == TK_WADAUTHOR );
      p_read_tk( parse );
      break;
   case TK_REGION:
   case TK_ENDREGION:
      p_read_tk( parse );
      break;
   case TK_MNEMONIC:
      p_read_mnemonic( parse );
      break;
   default:
      p_diag( parse, DIAG_POS_ERR, &pos,
         "unknown directive: %s", parse->tk_text );
      p_bail( parse );
   }
}

enum tk determine_bcs_pseudo_dirc( struct parse* parse ) {
   static const struct { const char* text; enum tk tk; } table[] = {
      { "libdefine", TK_LIBDEFINE },
      { "import", TK_IMPORT },
      { "library", TK_LIBRARY },
      { "encryptstrings", TK_ENCRYPTSTRINGS },
      { "nocompact", TK_NOCOMPACT },
      { "wadauthor", TK_WADAUTHOR },
      { "nowadauthor", TK_NOWADAUTHOR },
      { "mnemonic", TK_MNEMONIC },
   };
   for ( int i = 0; i < ARRAY_SIZE( table ); ++i ) {
      if ( strcmp( parse->tk_text, table[ i ].text ) == 0 ) {
         return table[ i ].tk;
      }
   }
   return TK_NONE;
}

void read_include( struct parse* parse ) {
   p_test_tk( parse, TK_INCLUDE );
   p_read_tk( parse );
   if ( parse->lib->imported ) {
      p_test_tk( parse, TK_LIT_STRING );
      p_read_tk( parse );
   }
   else {
      p_test_tk( parse, TK_LIT_STRING );
      p_load_included_source( parse, parse->tk_text, &parse->tk_pos );
      p_read_tk( parse );
   }
}

void read_define( struct parse* parse ) {
   bool define = ( parse->tk == TK_DEFINE );
   p_test_tk( parse, define ? TK_DEFINE : TK_LIBDEFINE );
   p_read_tk( parse );
   p_test_tk( parse, TK_ID );
   struct constant* constant = t_alloc_constant();
   constant->object.pos = parse->tk_pos;
   constant->name = t_extend_name( parse->ns->body, parse->tk_text );
   p_read_tk( parse );
   struct expr_reading value;
   p_init_expr_reading( &value, true, false, false, true );
   p_read_expr( parse, &value );
   constant->value_node = value.output_node;
   constant->hidden = define;
   p_add_unresolved( parse, &constant->object );
   list_append( &parse->lib->objects, constant );
   list_append( &parse->ns->objects, constant );
}

void read_import( struct parse* parse, struct pos* pos ) {
   p_test_tk( parse, parse->lang == LANG_BCS ? TK_ID : TK_IMPORT );
   p_read_tk( parse );
   p_test_tk( parse, TK_LIT_STRING );
   struct import_dirc* dirc = mem_alloc( sizeof( *dirc ) );
   dirc->file_path = parse->tk_text;
   dirc->pos = parse->tk_pos;
   list_append( &parse->lib->import_dircs, dirc ); 
   p_read_tk( parse );
}

void read_library( struct parse* parse, struct pos* pos, bool first_object ) {
   p_test_tk( parse, parse->lang == LANG_BCS ? TK_ID : TK_LIBRARY );
   p_read_tk( parse );
   parse->lib->header = true;
   if ( parse->tk == TK_LIT_STRING ) {
      read_library_name( parse, pos );
   }
   else {
      parse->lib->compiletime = true;
   }
   // In ACS, the #library header must be the first object in the module. In
   // BCS, it can appear anywhere.
   if ( parse->lang == LANG_ACS && ! first_object ) {
      p_diag( parse, DIAG_POS_ERR, &parse->tk_pos,
         "`library` directive not at the very top" );
      p_bail( parse );
   }
}

void read_library_name( struct parse* parse, struct pos* pos ) {
   p_test_tk( parse, TK_LIT_STRING );
   if ( ! parse->tk_length ) {
      p_diag( parse, DIAG_POS_ERR, &parse->tk_pos,
         "library name is blank" );
      p_bail( parse );
   }
   int length = parse->tk_length;
   if ( length > MAX_LIB_NAME_LENGTH ) {
      p_diag( parse, DIAG_WARN | DIAG_FILE | DIAG_LINE | DIAG_COLUMN,
         &parse->tk_pos, "library name too long" );
      p_diag( parse, DIAG_FILE, &parse->tk_pos,
         "library name can be up to %d characters long",
         MAX_LIB_NAME_LENGTH );
      length = MAX_LIB_NAME_LENGTH;
   }
   char name[ MAX_LIB_NAME_LENGTH + 1 ];
   memcpy( name, parse->tk_text, length );
   name[ length ] = 0;
   p_read_tk( parse );
   // Different #library directives in the same library must have the same
   // name.
   if ( parse->lib->name.length &&
      strcmp( parse->lib->name.value, name ) != 0 ) {
      p_diag( parse, DIAG_POS_ERR, pos, "library has multiple names" );
      p_diag( parse, DIAG_FILE | DIAG_LINE | DIAG_COLUMN,
         &parse->lib->name_pos, "first name given here" );
      p_bail( parse );
   }
   // Each library must have a unique name.
   list_iter_t i;
   list_iter_init( &i, &parse->task->libraries );
   while ( ! list_end( &i ) ) {
      struct library* lib = list_data( &i );
      if ( lib != parse->lib && strcmp( name, lib->name.value ) == 0 ) {
         p_diag( parse, DIAG_POS_ERR, pos,
            "duplicate library name" );
         p_diag( parse, DIAG_FILE | DIAG_LINE | DIAG_COLUMN, &lib->name_pos,
            "library name previously found here" );
         p_bail( parse );
      }
      list_next( &i );
   }
   str_copy( &parse->lib->name, name, length );
   parse->lib->importable = true;
   parse->lib->name_pos = *pos;
}

void p_add_unresolved( struct parse* parse, struct object* object ) {
   t_append_unresolved_namespace_object( parse->ns, object );
}

void read_imported_libs( struct parse* parse ) {
   list_iter_t i;
   list_iter_init( &i, &parse->lib->import_dircs );
   while ( ! list_end( &i ) ) {
      import_lib( parse, list_data( &i ) );
      list_next( &i );
   }
}

void import_lib( struct parse* parse, struct import_dirc* dirc ) {
   struct file_query query;
   t_init_file_query( &query, parse->task->library_main->file,
      dirc->file_path );
   t_find_file( parse->task, &query );
   if ( ! query.file ) {
      p_diag( parse, DIAG_POS_ERR, &dirc->pos,
         "library not found: %s", dirc->file_path );
      p_bail( parse );
   }
   // See if the library is already loaded.
   struct library* lib = NULL;
   list_iter_t i;
   list_iter_init( &i, &parse->task->libraries );
   while ( ! list_end( &i ) ) {
      lib = list_data( &i );
      if ( lib->file == query.file ) {
         goto have_lib;
      }
      list_next( &i );
   }
   // Load library from cache.
   if ( parse->cache ) {
      lib = cache_get( parse->cache, query.file );
      if ( lib ) {
         goto have_lib;
      }
   }
   // Read library from source file.
   lib = t_add_library( parse->task );
   lib->lang = p_determine_lang_from_file_path( query.file->full_path.value );
   if ( lib->lang == LANG_BCS && parse->lib->lang == LANG_ACS ) {
      p_diag( parse, DIAG_POS_ERR, &dirc->pos,
         "importing BCS library into ACS library" );
      p_bail( parse );
   }
   parse->lang = lib->lang;
   parse->lang_limits = t_get_lang_limits( lib->lang );
   parse->lib = lib;
   p_load_imported_lib_source( parse, dirc, query.file );
   lib->imported = true;
   parse->ns = lib->upmost_ns;
   p_clear_macros( parse );
   p_define_imported_macro( parse );
   p_define_cmdline_macros( parse );
   p_read_tk( parse );
   read_module( parse );
   parse->lib = parse->task->library_main;
   // An imported library must have a #library directive.
   if ( ! lib->header ) {
      p_diag( parse, DIAG_POS_ERR, &dirc->pos,
         "imported library missing #library directive" );
      p_bail( parse );
   }
   if ( parse->cache ) {
      cache_add( parse->cache, lib );
   }
   have_lib:
   // Append library.
   list_iter_init( &i, &parse->task->library_main->dynamic );
   while ( ! list_end( &i ) ) {
      struct library* another_lib = list_data( &i );
      if ( lib == another_lib ) {
         p_diag( parse, DIAG_WARN | DIAG_POS, &dirc->pos,
            "duplicate import of library" );
         return;
      }
      list_next( &i );
   }
   list_append( &parse->task->library_main->dynamic, lib );
}