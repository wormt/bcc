#include "phase.h"

static void read_peeked_token( struct parse* parse );
static void read_token( struct parse* parse );
static struct token* push_token( struct parse* parse );

// Functions used by the parser.
// ==========================================================================

// Needs to:
//  - Skip whitespace, both horizontal space and newlines.
//  - Expand macros.
//  - Execute directives.
void p_read_tk( struct parse* parse ) {
   read_peeked_token( parse );
   struct token* token = parse->token;
   parse->tk = token->type;
   parse->tk_text = token->text;
   parse->tk_pos = token->pos;
   parse->tk_length = token->length;
}

void read_peeked_token( struct parse* parse ) {
   if ( parse->parser_tkque.size > 0 ) {
      parse->token = p_shift_entry( parse, &parse->parser_tkque );
   }
   else {
      read_token( parse );
   }
}

void read_token( struct parse* parse ) {
   top:
   p_read_stream( parse );
   if ( parse->token->is_id ) {
      if ( p_expand_macro( parse ) ) {
         goto top;
      }
   }
   else {
      switch ( parse->token->type ) {
      case TK_HASH:
         if ( parse->line_beginning ) {
            if ( p_read_dirc( parse ) ) {
               goto top;
            }
         }
         break;
      case TK_NL:
         if ( ! parse->create_nltk ) {
            goto top;
         }
         break;
      case TK_HORZSPACE:
         goto top;
      default:
         break;
      }
   }
}

enum tk p_peek( struct parse* parse ) {
   return p_peek_tk( parse )->type;
}

struct token* p_peek_tk( struct parse* parse ) {
   struct parsertk_iter iter;
   p_init_parsertk_iter( parse, &iter );
   p_next_tk( parse, &iter );
   return iter.token;
}

void p_init_parsertk_iter( struct parse* parse, struct parsertk_iter* iter ) {
   iter->entry = parse->parser_tkque.head;
   iter->token = NULL;
}

void p_next_tk( struct parse* parse, struct parsertk_iter* iter ) {
   if ( iter->entry ) {
      struct queue_entry* entry = iter->entry;
      iter->token = entry->token;
      iter->entry = entry->next;
   }
   else {
      iter->token = push_token( parse );
   }
}

struct token* push_token( struct parse* parse ) {
   struct token* token = parse->token;
   struct token* source_token = parse->source_token;
   parse->source_token = &parse->token_peeked;
   read_token( parse );
   struct queue_entry* entry = p_push_entry( parse, &parse->parser_tkque );
   entry->token_allocated = ( parse->token == &parse->token_peeked );
   if ( entry->token_allocated ) {
      entry->token = p_alloc_token( parse );
      *entry->token = *parse->token;
   }
   else {
      entry->token = parse->token;
   }
   parse->source_token = source_token;
   parse->token = token;
   return entry->token;
}

/*
void read_stream( struct parse* parse ) {
   // Sometimes, the stream allocates a token dynamically. When a new token is
   // read, the previous token is no longer needed, so deallocate it.
   if ( parse->token->stream_alloced ) {
      free_token( parse, parse->token );
   }
   p_read_stream( parse );
   parse->token = parse->stream_token;
}
*/

void p_test_tk( struct parse* parse, enum tk expected ) {
   if ( parse->token->type != expected ) {
      if ( parse->token->type == TK_RESERVED ) {
         p_diag( parse, DIAG_POS_ERR, &parse->token->pos,
            "`%s` is a reserved identifier that is not currently used",
            parse->token->text );
      }
      else {
         p_diag( parse, DIAG_POS_ERR | DIAG_SYNTAX, &parse->token->pos, 
            "unexpected %s", p_get_token_name( parse->token->type ) );
         p_diag( parse, DIAG_FILE | DIAG_LINE | DIAG_COLUMN,
            &parse->token->pos,
            "expecting %s here", p_get_token_name( expected ),
            p_get_token_name( parse->token->type ) );
      }
      p_bail( parse );
   }
}

void p_skip_block( struct parse* parse ) {
   while ( parse->tk != TK_END && parse->tk != TK_BRACE_L ) {
      p_read_tk( parse );
   }
   p_test_tk( parse, TK_BRACE_L );
   p_read_tk( parse );
   int depth = 0;
   while ( true ) {
      if ( parse->tk == TK_BRACE_L ) {
         ++depth;
         p_read_tk( parse );
      }
      else if ( parse->tk == TK_BRACE_R ) {
         if ( depth ) {
            --depth;
            p_read_tk( parse );
         }
         else {
            break;
         }
      }
      else if ( parse->tk == TK_LIB_END ) {
         break;
      }
      else if ( parse->tk == TK_END ) {
         break;
      }
      else {
         p_read_tk( parse );
      }
   }
   p_test_tk( parse, TK_BRACE_R );
   p_read_tk( parse );
}

// Functions used by the preprocessor.
// ==========================================================================

void p_read_preptk( struct parse* parse ) {
   top:
   p_read_stream( parse );
   if ( parse->token->type == TK_HORZSPACE ) {
      goto top;
   }
}

void p_read_expanpreptk( struct parse* parse ) {
   top:
   p_read_stream( parse );
   if ( parse->token->is_id ) {
      if ( p_expand_macro( parse ) ) {
         goto top;
      }
   }
   else {
      switch ( parse->token->type ) {
      case TK_HORZSPACE:
         goto top;
      default:
         break;
      }
   }
}

void p_read_eoptiontk( struct parse* parse ) {
   top:
   p_read_stream( parse );
   if ( parse->token->is_id ) {
      if ( p_expand_macro( parse ) ) {
         goto top;
      }
   }
   else {
      switch ( parse->token->type ) {
      case TK_HASH:
         if ( parse->line_beginning ) {
            if ( p_read_dirc( parse ) ) {
               goto top;
            }
         }
         break;
      default:
         break;
      }
   }
}

void p_test_preptk( struct parse* parse, enum tk expected ) {
   if ( expected == TK_ID ) {
      if ( ! parse->token->is_id ) {
         p_test_tk( parse, TK_ID );
      }
   }
   else {
      p_test_tk( parse, expected );
   }
}

// ==========================================================================

/*
   // Concatenate adjacent strings.
   if ( parse->token->type == TK_LIT_STRING ) {
      struct str string;
      str_init( &string );
      str_append( &string, parse->token->text );
      while ( p_peek_tk( parse )->type == TK_LIT_STRING ) {
         readtk_peeked( parse );
         str_append( &string, parse->token->text );
printf( "t %d\n", parse->token->type );
         
      }
      if ( string.length ) {
   printf( "string: %s\n", string.value );
      }
   }*/

/*
   // -----------------------------------------------------------------------
   done:
   // Certain identifiers can appear only in a specific context.
   if ( parse->token->is_id &&
      strcmp( parse->token->text, "__va_args__" ) == 0 &&
      ! parse->prep_context != PREPCONTEXT_DEFINEBODY ) {
      p_diag( parse, DIAG_POS_ERR, &parse->token->pos,
         "`%s` outside function-like macro expansion",
         parse->token->text );
      p_bail( parse );
   }
*/