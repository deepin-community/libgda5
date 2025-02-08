/* Driver template for the LEMON parser generator.
** The author disclaims copyright to this source code.
*/
/* First off, code is included that follows the "include" declaration
** in the input grammar file. */
#include <stdio.h>
#line 36 "./parser.y"

#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>
#include <libgda/sql-parser/gda-sql-parser-private.h>
#include <libgda/sql-parser/gda-statement-struct-util.h>
#include <libgda/sql-parser/gda-statement-struct-trans.h>
#include <libgda/sql-parser/gda-statement-struct-insert.h>
#include <libgda/sql-parser/gda-statement-struct-update.h>
#include <libgda/sql-parser/gda-statement-struct-delete.h>
#include <libgda/sql-parser/gda-statement-struct-select.h>
#include <libgda/sql-parser/gda-statement-struct-compound.h>
#include <libgda/sql-parser/gda-statement-struct-parts.h>
#include <assert.h>

typedef struct {
	GValue *fname;
	GdaSqlExpr *expr;
} UpdateSet;

typedef struct {
	gboolean    distinct;
	GdaSqlExpr *expr;
} Distinct;

typedef struct {
	GdaSqlExpr *count;
	GdaSqlExpr *offset;
} Limit;

typedef struct {
	GSList *when_list;
	GSList *then_list;
} CaseBody;

static GdaSqlOperatorType
sql_operation_string_to_operator (const gchar *op)
{
	switch (g_ascii_toupper (*op)) {
	case 'A':
		return GDA_SQL_OPERATOR_TYPE_AND;
	case 'O':
		return GDA_SQL_OPERATOR_TYPE_OR;
	case 'N':
		return GDA_SQL_OPERATOR_TYPE_NOT;
	case '=':
		return GDA_SQL_OPERATOR_TYPE_EQ;
	case 'I':
		if (op[1] == 'S')
			return GDA_SQL_OPERATOR_TYPE_IS;
		else if (op[1] == 'N')
			return GDA_SQL_OPERATOR_TYPE_IN;
		break;
	case 'L':
		return GDA_SQL_OPERATOR_TYPE_LIKE;
	case 'B':
		return GDA_SQL_OPERATOR_TYPE_BETWEEN;
	case '>':
		if (op[1] == '=')
			return GDA_SQL_OPERATOR_TYPE_GEQ;
		else if (op[1] == 0)
			return GDA_SQL_OPERATOR_TYPE_GT;
		break;
	case '<':
		if (op[1] == '=')
			return GDA_SQL_OPERATOR_TYPE_LEQ;
		else if (op[1] == '>')
			return GDA_SQL_OPERATOR_TYPE_DIFF;
		else if (op[1] == 0)
			return GDA_SQL_OPERATOR_TYPE_LT;
		break;
	case '!':
		if (op[1] == '=')
			return GDA_SQL_OPERATOR_TYPE_DIFF;
		else if (op[1] == '~') {
			if (op[2] == 0)
				return GDA_SQL_OPERATOR_TYPE_NOT_REGEXP;
			else if (op[2] == '*')
				return GDA_SQL_OPERATOR_TYPE_NOT_REGEXP_CI;
		}
		break;
	case '~':
		if (op[1] == '*')
			return GDA_SQL_OPERATOR_TYPE_REGEXP_CI;
		else if (op[1] == 0)
			return GDA_SQL_OPERATOR_TYPE_REGEXP;
		break;
	case 'S':
		return GDA_SQL_OPERATOR_TYPE_SIMILAR;
	case '|':
		if (op[1] == '|')
			return GDA_SQL_OPERATOR_TYPE_CONCAT;
		else
			return GDA_SQL_OPERATOR_TYPE_BITOR;
	case '+':
		return GDA_SQL_OPERATOR_TYPE_PLUS;
	case '-':
		return GDA_SQL_OPERATOR_TYPE_MINUS;
	case '*':
		return GDA_SQL_OPERATOR_TYPE_STAR;
	case '/':
		return GDA_SQL_OPERATOR_TYPE_DIV;
	case '%':
		return GDA_SQL_OPERATOR_TYPE_REM;
	case '&':
		return GDA_SQL_OPERATOR_TYPE_BITAND;
	}
	g_error ("Unhandled operator named '%s'\n", op);
	return 0;
}

static GdaSqlOperatorType
string_to_op_type (GValue *value)
{
	GdaSqlOperatorType op;
	op = sql_operation_string_to_operator (g_value_get_string (value));
	g_value_reset (value);
	g_free (value);
	return op;
}

static GdaSqlExpr *
compose_multiple_expr (GdaSqlOperatorType op, GdaSqlExpr *left, GdaSqlExpr *right) {
	GdaSqlExpr *ret;
	if (left->cond && (left->cond->operator_type == op)) {
		ret = left;
		ret->cond->operands = g_slist_append (ret->cond->operands, right);
	}
	else {
		GdaSqlOperation *cond;
		ret = gda_sql_expr_new (NULL);
		cond = gda_sql_operation_new (GDA_SQL_ANY_PART (ret));
		ret->cond = cond;
		cond->operator_type = op;
		cond->operands = g_slist_prepend (NULL, right);
		GDA_SQL_ANY_PART (right)->parent = GDA_SQL_ANY_PART (cond);
		cond->operands = g_slist_prepend (cond->operands, left);
		GDA_SQL_ANY_PART (left)->parent = GDA_SQL_ANY_PART (cond);
	}
	return ret;
}

static GdaSqlExpr *
create_two_expr (GdaSqlOperatorType op, GdaSqlExpr *left, GdaSqlExpr *right) {
	GdaSqlExpr *ret;
	GdaSqlOperation *cond;
	ret = gda_sql_expr_new (NULL);
	cond = gda_sql_operation_new (GDA_SQL_ANY_PART (ret));
	ret->cond = cond;
	cond->operator_type = op;
	cond->operands = g_slist_prepend (NULL, right);
	GDA_SQL_ANY_PART (right)->parent = GDA_SQL_ANY_PART (cond);
	cond->operands = g_slist_prepend (cond->operands, left);
	GDA_SQL_ANY_PART (left)->parent = GDA_SQL_ANY_PART (cond);
	return ret;
}

static GdaSqlExpr *
create_uni_expr (GdaSqlOperatorType op, GdaSqlExpr *expr) {
	GdaSqlExpr *ret;
	GdaSqlOperation *cond;
	ret = gda_sql_expr_new (NULL);
	cond = gda_sql_operation_new (GDA_SQL_ANY_PART (ret));
	ret->cond = cond;
	cond->operator_type = op;
	cond->operands = g_slist_prepend (NULL, expr);
	GDA_SQL_ANY_PART (expr)->parent = GDA_SQL_ANY_PART (cond);
	return ret;
}

static GdaSqlStatement *
compose_multiple_compounds (GdaSqlStatementCompoundType ctype, GdaSqlStatement *left, GdaSqlStatement *right) {
	GdaSqlStatement *ret = NULL;
	GdaSqlStatementCompound *lc = (GdaSqlStatementCompound*) left->contents;
	if (lc->compound_type == ctype) {
		GdaSqlStatementCompound *rc = (GdaSqlStatementCompound*) right->contents;
		if (!rc->stmt_list->next || rc->compound_type == ctype) {
			GSList *list;
			for (list = rc->stmt_list; list; list = list->next)
				GDA_SQL_ANY_PART (((GdaSqlStatement*)list->data)->contents)->parent = GDA_SQL_ANY_PART (lc);

			ret = left;
			lc->stmt_list = g_slist_concat (lc->stmt_list, rc->stmt_list);
			rc->stmt_list = NULL;
			gda_sql_statement_free (right);
		}
	}
	else {
		ret = gda_sql_statement_new (GDA_SQL_STATEMENT_COMPOUND);
		gda_sql_statement_compound_set_type (ret, ctype);
		gda_sql_statement_compound_take_stmt (ret, left);
		gda_sql_statement_compound_take_stmt (ret, right);
	}
	return ret;
}

#line 206 "parser.c"
/* Next is all token values, in a form suitable for use by makeheaders.
** This section will be null unless lemon is run with the -m switch.
*/
/* 
** These constants (all generated automatically by the parser generator)
** specify the various kinds of tokens (terminals) that the parser
** understands. 
**
** Each symbol here is a terminal symbol in the grammar.
*/
/* Make sure the INTERFACE macro is defined.
*/
#ifndef INTERFACE
# define INTERFACE 1
#endif
/* The next thing included is series of defines which control
** various aspects of the generated parser.
**    YYCODETYPE         is the data type used for storing terminal
**                       and nonterminal numbers.  "unsigned char" is
**                       used if there are fewer than 250 terminals
**                       and nonterminals.  "int" is used otherwise.
**    YYNOCODE           is a number of type YYCODETYPE which corresponds
**                       to no legal terminal or nonterminal number.  This
**                       number is used to fill in empty slots of the hash 
**                       table.
**    YYFALLBACK         If defined, this indicates that one or more tokens
**                       have fall-back values which should be used if the
**                       original value of the token will not parse.
**    YYACTIONTYPE       is the data type used for storing terminal
**                       and nonterminal numbers.  "unsigned char" is
**                       used if there are fewer than 250 rules and
**                       states combined.  "int" is used otherwise.
**    gda_lemon_oracle_parserTOKENTYPE     is the data type used for minor tokens given 
**                       directly to the parser from the tokenizer.
**    YYMINORTYPE        is the data type used for all minor tokens.
**                       This is typically a union of many types, one of
**                       which is gda_lemon_oracle_parserTOKENTYPE.  The entry in the union
**                       for base tokens is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    gda_lemon_oracle_parserARG_SDECL     A static variable declaration for the %extra_argument
**    gda_lemon_oracle_parserARG_PDECL     A parameter declaration for the %extra_argument
**    gda_lemon_oracle_parserARG_STORE     Code to store %extra_argument into yypParser
**    gda_lemon_oracle_parserARG_FETCH     Code to extract %extra_argument from yypParser
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
*/
#define YYCODETYPE unsigned char
#define YYNOCODE 209
#define YYACTIONTYPE unsigned short int
#define gda_lemon_oracle_parserTOKENTYPE GValue *
typedef union {
  int yyinit;
  gda_lemon_oracle_parserTOKENTYPE yy0;
  GSList * yy33;
  CaseBody yy51;
  gboolean yy100;
  GSList* yy105;
  GdaTransactionIsolation yy197;
  Distinct * yy249;
  GdaSqlStatement * yy252;
  GdaSqlSelectFrom * yy259;
  GdaSqlOperatorType yy295;
  GdaSqlExpr * yy302;
  GdaSqlParamSpec * yy303;
  GdaSqlExpr* yy354;
  GdaSqlSelectJoinType yy367;
  Limit yy408;
  GdaSqlSelectTarget * yy414;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define gda_lemon_oracle_parserARG_SDECL GdaSqlParserIface *pdata;
#define gda_lemon_oracle_parserARG_PDECL ,GdaSqlParserIface *pdata
#define gda_lemon_oracle_parserARG_FETCH GdaSqlParserIface *pdata = yypParser->pdata
#define gda_lemon_oracle_parserARG_STORE yypParser->pdata = pdata
#define YYNSTATE 361
#define YYNRULE 196
#define YYFALLBACK 1
#define YY_NO_ACTION      (YYNSTATE+YYNRULE+2)
#define YY_ACCEPT_ACTION  (YYNSTATE+YYNRULE+1)
#define YY_ERROR_ACTION   (YYNSTATE+YYNRULE)

/* The yyzerominor constant is used to initialize instances of
** YYMINORTYPE objects to zero. */
static const YYMINORTYPE yyzerominor = { 0 };

/* Define the yytestcase() macro to be a no-op if is not already defined
** otherwise.
**
** Applications can choose to define yytestcase() in the %include section
** to a macro that can assist in verifying code coverage.  For production
** code the yytestcase() macro should be turned off.  But it is useful
** for testing.
*/
#ifndef yytestcase
# define yytestcase(X)
#endif


/* Next are the tables used to determine what action to take based on the
** current state and lookahead token.  These tables are used to implement
** functions that take a state number and lookahead value and return an
** action integer.  
**
** Suppose the action integer is N.  Then the action is determined as
** follows
**
**   0 <= N < YYNSTATE                  Shift N.  That is, push the lookahead
**                                      token onto the stack and goto state N.
**
**   YYNSTATE <= N < YYNSTATE+YYNRULE   Reduce by rule N-YYNSTATE.
**
**   N == YYNSTATE+YYNRULE              A syntax error has occurred.
**
**   N == YYNSTATE+YYNRULE+1            The parser accepts its input.
**
**   N == YYNSTATE+YYNRULE+2            No such action.  Denotes unused
**                                      slots in the yy_action[] table.
**
** The action table is constructed as a single large table named yy_action[].
** Given state S and lookahead X, the action is computed as
**
**      yy_action[ yy_shift_ofst[S] + X ]
**
** If the index value yy_shift_ofst[S]+X is out of range or if the value
** yy_lookahead[yy_shift_ofst[S]+X] is not equal to X or if yy_shift_ofst[S]
** is equal to YY_SHIFT_USE_DFLT, it means that the action is not in the table
** and that yy_default[S] should be used instead.  
**
** The formula above is for computing the action when the lookahead is
** a terminal symbol.  If the lookahead is a non-terminal (as occurs after
** a reduce action) then the yy_reduce_ofst[] array is used in place of
** the yy_shift_ofst[] array and YY_REDUCE_USE_DFLT is used in place of
** YY_SHIFT_USE_DFLT.
**
** The following are the tables generated in this section:
**
**  yy_action[]        A single table containing all actions.
**  yy_lookahead[]     A table containing the lookahead for each entry in
**                     yy_action.  Used to detect hash collisions.
**  yy_shift_ofst[]    For each state, the offset into yy_action for
**                     shifting terminals.
**  yy_reduce_ofst[]   For each state, the offset into yy_action for
**                     shifting non-terminals after a reduce.
**  yy_default[]       Default action for each state.
*/
#define YY_ACTTAB_COUNT (1400)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    39,  243,  324,  240,  558,  143,   22,  121,  361,  101,
 /*    10 */    36,   41,   41,   41,   41,   37,   37,   37,   37,   37,
 /*    20 */    48,   45,   45,  310,  357,   50,   50,   49,   47,   47,
 /*    30 */    42,  362,  241,  350,  349,  161,  348,  347,  346,  345,
 /*    40 */    43,   44,  199,   13,   38,  235,  335,   76,   40,   40,
 /*    50 */    36,   41,   41,   41,   41,   37,   37,   37,   37,   37,
 /*    60 */   223,   45,   45,  319,  167,   50,   50,   49,   47,   47,
 /*    70 */    42,  353,  318,  314,  225,  224,  196,  195,  194,  226,
 /*    80 */   243,  287,  201,  287,   39,  134,  285,   76,  285,  135,
 /*    90 */   315,  286,  292,  286,   21,   41,   41,   41,   41,   37,
 /*   100 */    37,   37,   37,   37,  219,   45,   45,  308,  171,   50,
 /*   110 */    50,   49,   47,   47,   42,  280,  191,  232,  216,  215,
 /*   120 */   216,  192,   34,  192,   43,   44,  199,   13,   38,  235,
 /*   130 */   335,   76,   40,   40,   36,   41,   41,   41,   41,   37,
 /*   140 */    37,   37,   37,   37,  222,   45,   45,  133,  353,   50,
 /*   150 */    50,   49,   47,   47,   42,   39,   37,  320,   45,   45,
 /*   160 */   284,  283,   50,   50,   49,   47,   47,   42,  278,  277,
 /*   170 */   338,   76,  329,  354,  348,  347,  346,  345,  306,  289,
 /*   180 */   136,  127,  358,  129,   76,  276,   52,  169,  168,  167,
 /*   190 */   169,  168,  167,  360,  359,   43,   44,  199,   13,   38,
 /*   200 */   235,  335,   42,   40,   40,   36,   41,   41,   41,   41,
 /*   210 */    37,   37,   37,   37,   37,  221,   45,   45,  190,   76,
 /*   220 */    50,   50,   49,   47,   47,   42,   39,   28,  352,   26,
 /*   230 */   243,  344,  240,   37,   37,   37,   37,   37,   61,   45,
 /*   240 */    45,    1,   76,   50,   50,   49,   47,   47,   42,  229,
 /*   250 */   354,   50,   50,   49,   47,   47,   42,  275,  261,  260,
 /*   260 */   189,  241,  350,  276,  299,   76,   43,   44,  199,   13,
 /*   270 */    38,  235,  335,   76,   40,   40,   36,   41,   41,   41,
 /*   280 */    41,   37,   37,   37,   37,   37,  317,   45,   45,   64,
 /*   290 */   128,   50,   50,   49,   47,   47,   42,  179,  151,  338,
 /*   300 */   178,  265,  264,   39,  239,  352,  240,  243,  200,  240,
 /*   310 */   180,  198,  102,   76,  357,  102,  364,  357,  169,  168,
 /*   320 */   167,  360,  359,  343,  364,  364,  364,  118,   57,   54,
 /*   330 */   149,  169,  168,  167,  231,  241,  350,   46,  241,  350,
 /*   340 */   131,  122,   75,   43,   44,  199,   13,   38,  235,  335,
 /*   350 */    80,   40,   40,   36,   41,   41,   41,   41,   37,   37,
 /*   360 */    37,   37,   37,  339,   45,   45,  263,  262,   50,   50,
 /*   370 */    49,   47,   47,   42,  209,   66,   39,  243,  336,  240,
 /*   380 */     3,  142,   53,   51,  337,   93,   49,   47,   47,   42,
 /*   390 */    76,  326,  169,  168,  167,  295,   48,  372,  227,  169,
 /*   400 */   168,  167,   82,  170,  148,   55,   76,  204,  241,  350,
 /*   410 */   144,  372,  372,  142,  334,  282,   43,   44,  199,   13,
 /*   420 */    38,  235,  335,   27,   40,   40,   36,   41,   41,   41,
 /*   430 */    41,   37,   37,   37,   37,   37,    2,   45,   45,   56,
 /*   440 */   125,   50,   50,   49,   47,   47,   42,  177,  323,   39,
 /*   450 */   325,  322,  397,  353,  243,  353,  240,  154,  197,  169,
 /*   460 */   168,  167,  102,   76,  357,  169,  168,  167,   48,  287,
 /*   470 */   146,  169,  168,  167,  285,  243,  243,  301,  301,  286,
 /*   480 */    11,  193,  185,   20,   18,  241,  350,  353,   48,   43,
 /*   490 */    44,  199,   13,   38,  235,  335,   24,   40,   40,   36,
 /*   500 */    41,   41,   41,   41,   37,   37,   37,   37,   37,  192,
 /*   510 */    45,   45,   10,  302,   50,   50,   49,   47,   47,   42,
 /*   520 */   176,  353,  282,  181,  313,  312,   39,  243,  371,  240,
 /*   530 */   243,   74,  240,  311,  252,   88,   76,  357,   88,  243,
 /*   540 */   357,  137,  371,  371,  309,  341,  282,  340,  141,  243,
 /*   550 */   274,  355,   48,  152,  271,  354,  276,  354,  241,  350,
 /*   560 */   276,  241,  350,  156,    6,   55,   43,   44,  199,   13,
 /*   570 */    38,  235,  335,   80,   40,   40,   36,   41,   41,   41,
 /*   580 */    41,   37,   37,   37,   37,   37,  353,   45,   45,  354,
 /*   590 */   251,   50,   50,   49,   47,   47,   42,   39,  243,  307,
 /*   600 */   240,  239,  188,  240,  142,  332,  102,  331,   48,   63,
 /*   610 */   352,  269,  352,   76,  294,   60,  259,  276,  250,  338,
 /*   620 */   243,   77,  236,  354,  305,  243,   16,  303,  290,  241,
 /*   630 */   350,  293,  241,  350,  333,  330,   74,   43,   35,  199,
 /*   640 */    13,   38,  235,  335,  352,   40,   40,   36,   41,   41,
 /*   650 */    41,   41,   37,   37,   37,   37,   37,  353,   45,   45,
 /*   660 */   184,  279,   50,   50,   49,   47,   47,   42,   39,  243,
 /*   670 */   218,  240,  243,  187,  240,  182,  186,  102,  352,  217,
 /*   680 */   102,  182,  244,  213,   76,  356,  212,  357,  354,  267,
 /*   690 */     9,  268,  357,  243,  243,  147,  145,  276,  357,  357,
 /*   700 */   241,  350,  357,  241,  350,  150,  397,  123,   43,   33,
 /*   710 */   199,   13,   38,  235,  335,  183,   40,   40,   36,   41,
 /*   720 */    41,   41,   41,   37,   37,   37,   37,   37,  353,   45,
 /*   730 */    45,   70,  357,   50,   50,   49,   47,   47,   42,   39,
 /*   740 */   243,  175,  240,  352,  243,  258,  203,  243,  106,  298,
 /*   750 */   243,  276,  202,  207,    8,   76,    7,  249,  357,  354,
 /*   760 */    58,   68,   15,   14,  173,  172,  291,  160,   81,   79,
 /*   770 */   338,  241,  350,   78,   73,   51,  282,   76,  297,  158,
 /*   780 */    44,  199,   13,   38,  235,  335,  132,   40,   40,   36,
 /*   790 */    41,   41,   41,   41,   37,   37,   37,   37,   37,  140,
 /*   800 */    45,   45,   25,  353,   50,   50,   49,   47,   47,   42,
 /*   810 */    39,  243,  237,  240,  352,  243,  159,  240,  228,  120,
 /*   820 */    19,  155,   23,  138,  157,  230,   76,  321,  304,   17,
 /*   830 */   354,  220,  281,  216,  296,  273,   55,  270,   72,  174,
 /*   840 */   266,   65,  241,  350,  205,  210,  241,  350,   69,   59,
 /*   850 */   206,  208,  199,   13,   38,  235,  335,  248,   40,   40,
 /*   860 */    36,   41,   41,   41,   41,   37,   37,   37,   37,   37,
 /*   870 */    30,   45,   45,  353,   67,   50,   50,   49,   47,   47,
 /*   880 */    42,  243,  237,  240,  243,  352,  240,  246,  243,  105,
 /*   890 */   240,   83,  139,   31,   32,  342,   87,   76,  243,  166,
 /*   900 */   240,  234,   29,    4,  300,  354,  119,  153,  247,  242,
 /*   910 */   126,  351,  241,  350,  256,  241,  350,  255,  288,  241,
 /*   920 */   350,  332,  254,  331,  238,  124,  272,  257,  245,  241,
 /*   930 */   350,  353,  253,  316,  142,  559,  559,  559,  559,  559,
 /*   940 */    30,  559,  559,  559,  559,  327,   12,  353,  559,  353,
 /*   950 */   333,  330,  233,  559,  559,  328,  237,  559,  559,  559,
 /*   960 */   352,  559,  353,   31,   32,  559,  243,  559,  240,  559,
 /*   970 */   559,  237,   29,    5,  110,  354,  243,  559,  240,  559,
 /*   980 */   243,  559,  240,  559,  107,  559,  559,  559,  165,  130,
 /*   990 */   559,  332,  559,  331,  559,  559,  559,  241,  350,  559,
 /*  1000 */   559,  559,  559,  559,  559,  174,  559,  241,  350,  211,
 /*  1010 */   214,  241,  350,  559,   30,  559,   12,  559,  559,  559,
 /*  1020 */   333,  330,  233,  559,  243,  328,  240,  559,  559,   30,
 /*  1030 */   352,  353,  116,  354,  559,  559,  559,   31,   32,  342,
 /*  1040 */   237,  559,  559,  559,  559,  559,   29,    5,  559,  354,
 /*  1050 */   559,  354,   31,   32,  559,  241,  350,  559,  559,  559,
 /*  1060 */   559,   29,    4,  559,  354,  332,   71,  331,  559,  243,
 /*  1070 */   559,  240,  559,  559,  559,  559,  243,  114,  240,  559,
 /*  1080 */   332,  559,  331,  559,  113,  559,  559,  559,  352,  559,
 /*  1090 */    12,  559,  559,  142,  333,  330,  233,  559,   30,  328,
 /*  1100 */   241,  350,  559,  559,  352,   12,  352,  241,  350,  333,
 /*  1110 */   330,  233,  559,  559,  328,  559,  559,  559,  559,  352,
 /*  1120 */   559,   31,   32,  559,  559,  559,  243,  559,  240,  559,
 /*  1130 */    29,    5,  559,  354,  112,  243,  559,  240,  559,  243,
 /*  1140 */   559,  240,  559,  117,  243,  559,  240,  104,  559,  332,
 /*  1150 */   559,  331,  115,  243,  559,  240,  559,  241,  350,  559,
 /*  1160 */   559,  103,  559,  559,  559,  559,  241,  350,  559,  559,
 /*  1170 */   241,  350,  559,  559,   12,  241,  350,  559,  333,  330,
 /*  1180 */   233,  559,  559,  328,  241,  350,  559,  243,  352,  240,
 /*  1190 */   559,  243,  559,  240,  559,  109,  243,  559,  240,  164,
 /*  1200 */   559,  243,  559,  240,  163,  559,  559,  559,  243,  108,
 /*  1210 */   240,  559,  559,  559,  559,  559,  162,  559,  241,  350,
 /*  1220 */   559,  559,  241,  350,  559,  559,  559,  241,  350,  559,
 /*  1230 */   559,  559,  241,  350,  559,  243,  559,  240,  559,  241,
 /*  1240 */   350,  559,  243,   86,  240,  559,  559,  243,  559,  240,
 /*  1250 */   100,  243,  559,  240,  559,   99,  559,  559,  559,   85,
 /*  1260 */   559,  243,  559,  240,  559,  559,  241,  350,  559,   98,
 /*  1270 */   559,  559,  559,  241,  350,  559,  559,  559,  241,  350,
 /*  1280 */   559,  559,  241,  350,  559,  559,  559,  559,  559,  559,
 /*  1290 */   559,  559,  241,  350,  559,  559,  559,  243,  559,  240,
 /*  1300 */   559,  243,  559,  240,  243,   84,  240,  559,  243,   97,
 /*  1310 */   240,  559,   96,  559,  559,  243,   62,  240,  243,  559,
 /*  1320 */   240,  559,  243,   95,  240,  559,   94,  559,  241,  350,
 /*  1330 */    92,  559,  241,  350,  559,  241,  350,  559,  559,  241,
 /*  1340 */   350,  559,  559,  559,  559,  559,  241,  350,  559,  241,
 /*  1350 */   350,  559,  559,  241,  350,  243,  559,  240,  243,  559,
 /*  1360 */   240,  559,  243,   91,  240,  559,   90,  243,  559,  240,
 /*  1370 */    89,  559,  559,  559,  559,  111,  559,  559,  559,  559,
 /*  1380 */   559,  559,  559,  559,  559,  559,  241,  350,  559,  241,
 /*  1390 */   350,  559,  559,  241,  350,  559,  559,  559,  241,  350,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */    26,  170,  102,  172,  160,  161,   32,  163,    0,  178,
 /*    10 */    76,   77,   78,   79,   80,   81,   82,   83,   84,   85,
 /*    20 */   120,   87,   88,  103,  180,   91,   92,   93,   94,   95,
 /*    30 */    96,    0,  201,  202,  152,  204,  154,  155,  156,  157,
 /*    40 */    66,   67,   68,   69,   70,   71,   72,  113,   74,   75,
 /*    50 */    76,   77,   78,   79,   80,   81,   82,   83,   84,   85,
 /*    60 */   140,   87,   88,    5,  112,   91,   92,   93,   94,   95,
 /*    70 */    96,    1,   14,  103,  104,  105,  106,  107,  108,  109,
 /*    80 */   170,   13,  172,   13,   26,  141,   18,  113,   18,  179,
 /*    90 */   120,   23,  131,   23,  120,   77,   78,   79,   80,   81,
 /*   100 */    82,   83,   84,   85,  143,   87,   88,  103,  151,   91,
 /*   110 */    92,   93,   94,   95,   96,   52,   53,   71,   50,   56,
 /*   120 */    50,   53,   76,   53,   66,   67,   68,   69,   70,   71,
 /*   130 */    72,  113,   74,   75,   76,   77,   78,   79,   80,   81,
 /*   140 */    82,   83,   84,   85,  140,   87,   88,  141,    1,   91,
 /*   150 */    92,   93,   94,   95,   96,   26,   85,  190,   87,   88,
 /*   160 */    57,   58,   91,   92,   93,   94,   95,   96,   54,   55,
 /*   170 */   203,  113,  152,  103,  154,  155,  156,  157,  103,  164,
 /*   180 */   165,  166,  102,  141,  113,  170,  118,  110,  111,  112,
 /*   190 */   110,  111,  112,  116,  117,   66,   67,   68,   69,   70,
 /*   200 */    71,   72,   96,   74,   75,   76,   77,   78,   79,   80,
 /*   210 */    81,   82,   83,   84,   85,  140,   87,   88,   23,  113,
 /*   220 */    91,   92,   93,   94,   95,   96,   26,  145,  158,  147,
 /*   230 */   170,  102,  172,   81,   82,   83,   84,   85,  178,   87,
 /*   240 */    88,  101,  113,   91,   92,   93,   94,   95,   96,  189,
 /*   250 */   103,   91,   92,   93,   94,   95,   96,  164,   63,   64,
 /*   260 */    65,  201,  202,  170,    1,  113,   66,   67,   68,   69,
 /*   270 */    70,   71,   72,  113,   74,   75,   76,   77,   78,   79,
 /*   280 */    80,   81,   82,   83,   84,   85,  190,   87,   88,    8,
 /*   290 */   141,   91,   92,   93,   94,   95,   96,  163,   17,  203,
 /*   300 */   163,   63,   64,   26,  170,  158,  172,  170,  174,  172,
 /*   310 */   102,  174,  178,  113,  180,  178,  102,  180,  110,  111,
 /*   320 */   112,  116,  117,  102,  110,  111,  112,  192,  193,   48,
 /*   330 */    49,  110,  111,  112,  200,  201,  202,  101,  201,  202,
 /*   340 */    59,   60,  142,   66,   67,   68,   69,   70,   71,   72,
 /*   350 */   101,   74,   75,   76,   77,   78,   79,   80,   81,   82,
 /*   360 */    83,   84,   85,  102,   87,   88,   63,   64,   91,   92,
 /*   370 */    93,   94,   95,   96,  125,  120,   26,  170,  102,  172,
 /*   380 */   101,  132,  101,  128,  102,  178,   93,   94,   95,   96,
 /*   390 */   113,  102,  110,  111,  112,  120,  120,  102,  191,  110,
 /*   400 */   111,  112,  127,  151,  123,  142,  113,  126,  201,  202,
 /*   410 */   129,  116,  117,  132,   17,  120,   66,   67,   68,   69,
 /*   420 */    70,   71,   72,  146,   74,   75,   76,   77,   78,   79,
 /*   430 */    80,   81,   82,   83,   84,   85,  101,   87,   88,  165,
 /*   440 */   166,   91,   92,   93,   94,   95,   96,  163,  102,   26,
 /*   450 */   102,  102,   50,    1,  170,    1,  172,  102,  174,  110,
 /*   460 */   111,  112,  178,  113,  180,  110,  111,  112,  120,   13,
 /*   470 */   102,  110,  111,  112,   18,  170,  170,  172,  172,   23,
 /*   480 */   135,  176,  176,  120,  120,  201,  202,    1,  120,   66,
 /*   490 */    67,   68,   69,   70,   71,   72,  146,   74,   75,   76,
 /*   500 */    77,   78,   79,   80,   81,   82,   83,   84,   85,   53,
 /*   510 */    87,   88,  135,  102,   91,   92,   93,   94,   95,   96,
 /*   520 */   163,    1,  120,  163,  103,  103,   26,  170,  102,  172,
 /*   530 */   170,  120,  172,  103,  102,  178,  113,  180,  178,  170,
 /*   540 */   180,  172,  116,  117,  103,   93,  120,   93,  183,  170,
 /*   550 */   164,  172,  120,  167,  164,  103,  170,  103,  201,  202,
 /*   560 */   170,  201,  202,  194,  199,  142,   66,   67,   68,   69,
 /*   570 */    70,   71,   72,  101,   74,   75,   76,   77,   78,   79,
 /*   580 */    80,   81,   82,   83,   84,   85,    1,   87,   88,  103,
 /*   590 */   102,   91,   92,   93,   94,   95,   96,   26,  170,  103,
 /*   600 */   172,  170,  174,  172,  132,  119,  178,  121,  120,  178,
 /*   610 */   158,  164,  158,  113,  198,  101,  169,  170,  102,  203,
 /*   620 */   170,  101,  172,  103,  103,  170,  139,  172,  102,  201,
 /*   630 */   202,  200,  201,  202,  148,  149,  120,   66,   67,   68,
 /*   640 */    69,   70,   71,   72,  158,   74,   75,   76,   77,   78,
 /*   650 */    79,   80,   81,   82,   83,   84,   85,    1,   87,   88,
 /*   660 */    51,   53,   91,   92,   93,   94,   95,   96,   26,  170,
 /*   670 */   163,  172,  170,  174,  172,  163,  174,  178,  158,  161,
 /*   680 */   178,  163,  163,  119,  113,  163,  120,  180,  103,  121,
 /*   690 */   101,  164,  180,  170,  170,  172,  172,  170,  180,  180,
 /*   700 */   201,  202,  180,  201,  202,  119,   50,  122,   66,   67,
 /*   710 */    68,   69,   70,   71,   72,  163,   74,   75,   76,   77,
 /*   720 */    78,   79,   80,   81,   82,   83,   84,   85,    1,   87,
 /*   730 */    88,  124,  180,   91,   92,   93,   94,   95,   96,   26,
 /*   740 */   170,  163,  172,  158,  170,  164,  172,  170,  178,  172,
 /*   750 */   170,  170,  172,  120,  101,  113,  101,    1,  180,  103,
 /*   760 */   130,  127,   75,   75,  207,  207,  131,  177,  181,  181,
 /*   770 */   203,  201,  202,  181,  118,  128,  120,  113,  201,  186,
 /*   780 */    67,   68,   69,   70,   71,   72,   59,   74,   75,   76,
 /*   790 */    77,   78,   79,   80,   81,   82,   83,   84,   85,  205,
 /*   800 */    87,   88,  145,    1,   91,   92,   93,   94,   95,   96,
 /*   810 */    26,  170,   10,  172,  158,  170,  185,  172,  137,  178,
 /*   820 */   136,  195,  133,  178,  187,  134,  113,  188,  196,  139,
 /*   830 */   103,  138,  166,   50,  198,  166,  142,  118,  168,   57,
 /*   840 */   169,  168,  201,  202,   66,  171,  201,  202,  173,  101,
 /*   850 */   120,  175,   68,   69,   70,   71,   72,  177,   74,   75,
 /*   860 */    76,   77,   78,   79,   80,   81,   82,   83,   84,   85,
 /*   870 */    68,   87,   88,    1,  171,   91,   92,   93,   94,   95,
 /*   880 */    96,  170,   10,  172,  170,  158,  172,  162,  170,  178,
 /*   890 */   172,  182,  178,   91,   92,   93,  178,  113,  170,  184,
 /*   900 */   172,  206,  100,  101,  198,  103,  178,  167,  177,  170,
 /*   910 */   167,  170,  201,  202,  170,  201,  202,  170,  165,  201,
 /*   920 */   202,  119,  170,  121,  170,  167,  165,  170,  162,  201,
 /*   930 */   202,    1,  170,  197,  132,  208,  208,  208,  208,  208,
 /*   940 */    68,  208,  208,  208,  208,   73,  144,    1,  208,    1,
 /*   950 */   148,  149,  150,  208,  208,  153,   10,  208,  208,  208,
 /*   960 */   158,  208,    1,   91,   92,  208,  170,  208,  172,  208,
 /*   970 */   208,   10,  100,  101,  178,  103,  170,  208,  172,  208,
 /*   980 */   170,  208,  172,  208,  178,  208,  208,  208,  178,   59,
 /*   990 */   208,  119,  208,  121,  208,  208,  208,  201,  202,  208,
 /*  1000 */   208,  208,  208,  208,  208,   57,  208,  201,  202,   61,
 /*  1010 */    62,  201,  202,  208,   68,  208,  144,  208,  208,  208,
 /*  1020 */   148,  149,  150,  208,  170,  153,  172,  208,  208,   68,
 /*  1030 */   158,    1,  178,  103,  208,  208,  208,   91,   92,   93,
 /*  1040 */    10,  208,  208,  208,  208,  208,  100,  101,  208,  103,
 /*  1050 */   208,  103,   91,   92,  208,  201,  202,  208,  208,  208,
 /*  1060 */   208,  100,  101,  208,  103,  119,  118,  121,  208,  170,
 /*  1070 */   208,  172,  208,  208,  208,  208,  170,  178,  172,  208,
 /*  1080 */   119,  208,  121,  208,  178,  208,  208,  208,  158,  208,
 /*  1090 */   144,  208,  208,  132,  148,  149,  150,  208,   68,  153,
 /*  1100 */   201,  202,  208,  208,  158,  144,  158,  201,  202,  148,
 /*  1110 */   149,  150,  208,  208,  153,  208,  208,  208,  208,  158,
 /*  1120 */   208,   91,   92,  208,  208,  208,  170,  208,  172,  208,
 /*  1130 */   100,  101,  208,  103,  178,  170,  208,  172,  208,  170,
 /*  1140 */   208,  172,  208,  178,  170,  208,  172,  178,  208,  119,
 /*  1150 */   208,  121,  178,  170,  208,  172,  208,  201,  202,  208,
 /*  1160 */   208,  178,  208,  208,  208,  208,  201,  202,  208,  208,
 /*  1170 */   201,  202,  208,  208,  144,  201,  202,  208,  148,  149,
 /*  1180 */   150,  208,  208,  153,  201,  202,  208,  170,  158,  172,
 /*  1190 */   208,  170,  208,  172,  208,  178,  170,  208,  172,  178,
 /*  1200 */   208,  170,  208,  172,  178,  208,  208,  208,  170,  178,
 /*  1210 */   172,  208,  208,  208,  208,  208,  178,  208,  201,  202,
 /*  1220 */   208,  208,  201,  202,  208,  208,  208,  201,  202,  208,
 /*  1230 */   208,  208,  201,  202,  208,  170,  208,  172,  208,  201,
 /*  1240 */   202,  208,  170,  178,  172,  208,  208,  170,  208,  172,
 /*  1250 */   178,  170,  208,  172,  208,  178,  208,  208,  208,  178,
 /*  1260 */   208,  170,  208,  172,  208,  208,  201,  202,  208,  178,
 /*  1270 */   208,  208,  208,  201,  202,  208,  208,  208,  201,  202,
 /*  1280 */   208,  208,  201,  202,  208,  208,  208,  208,  208,  208,
 /*  1290 */   208,  208,  201,  202,  208,  208,  208,  170,  208,  172,
 /*  1300 */   208,  170,  208,  172,  170,  178,  172,  208,  170,  178,
 /*  1310 */   172,  208,  178,  208,  208,  170,  178,  172,  170,  208,
 /*  1320 */   172,  208,  170,  178,  172,  208,  178,  208,  201,  202,
 /*  1330 */   178,  208,  201,  202,  208,  201,  202,  208,  208,  201,
 /*  1340 */   202,  208,  208,  208,  208,  208,  201,  202,  208,  201,
 /*  1350 */   202,  208,  208,  201,  202,  170,  208,  172,  170,  208,
 /*  1360 */   172,  208,  170,  178,  172,  208,  178,  170,  208,  172,
 /*  1370 */   178,  208,  208,  208,  208,  178,  208,  208,  208,  208,
 /*  1380 */   208,  208,  208,  208,  208,  208,  201,  202,  208,  201,
 /*  1390 */   202,  208,  208,  201,  202,  208,  208,  208,  201,  202,
};
#define YY_SHIFT_USE_DFLT (-119)
#define YY_SHIFT_COUNT (246)
#define YY_SHIFT_MIN   (-118)
#define YY_SHIFT_MAX   (1030)
static const short yy_shift_ofst[] = {
 /*     0 */   281,  802,  961,  961,  961,  961,  946, 1030, 1030, 1030,
 /*    10 */  1030, 1030, 1030,  872, 1030, 1030, 1030, 1030, 1030, 1030,
 /*    20 */  1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030,
 /*    30 */  1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030,
 /*    40 */  1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030, 1030,
 /*    50 */  1030, 1030,   70,  281,  948,  486,  656,  520,  147,  147,
 /*    60 */   147,   58,   58,  423,   68,  585,  147,  147,  147,  249,
 /*    70 */   147,  147,  147,  147,  147,  147,  147,  472,  472,  472,
 /*    80 */   472,  472, -119, -119,  -26,  350,  277,  200,  129,  500,
 /*    90 */   500,  500,  500,  500,  500,  500,  500,  500,  500,  500,
 /*   100 */   500,  500,  500,  642,  571,  500,  500,  713,  784,  784,
 /*   110 */   784,  -66,  -66,  -66,  -66,   18,  152,   71,  -30,  160,
 /*   120 */   293,   77,  930,  727,  456,  426,  456,  295,  454,  452,
 /*   130 */   147,  147,  147,  147,  147,  255,  402,  263,  106,  106,
 /*   140 */    82,  275,  -39,  205,  778,  647,  730,  748,  778,  719,
 /*   150 */   782,  719,  783,  783,  694,  693,  690,  689,  691,  684,
 /*   160 */   681,  657,  664,  664,  664,  664,  647,  635,  635,  635,
 /*   170 */  -119, -119,   20, -118,  195,  355,  349,  289,  282,  221,
 /*   180 */   214,  208,   80,  361,   63,  516,  488,  432,  368,  303,
 /*   190 */   238,  114,  103,  411,   75,    4,  -80,  348,  276,   46,
 /*   200 */  -100,  688,  687,  630,  634,  756,  655,  653,  633,  589,
 /*   210 */   607,  586,  568,  566,  564,  608,  609,  526,  -48,  487,
 /*   220 */   514,  521,  496,  441,  430,  422,  421,  364,  377,  363,
 /*   230 */   345,  346,  335,  252,  397,  279,  261,  236,  149,   42,
 /*   240 */   140,  -43,    6,  -56,  -48,   31,    8,
};
#define YY_REDUCE_USE_DFLT (-170)
#define YY_REDUCE_COUNT (171)
#define YY_REDUCE_MIN   (-169)
#define YY_REDUCE_MAX   (1197)
static const short yy_reduce_ofst[] = {
 /*     0 */  -156,  134,  284,  137,  360,  357,  431,  502,  499,  428,
 /*    10 */   207,   60, -169, 1197, 1192, 1188, 1185, 1152, 1148, 1145,
 /*    20 */  1138, 1134, 1131, 1127, 1091, 1081, 1077, 1072, 1065, 1038,
 /*    30 */  1031, 1026, 1021, 1017,  983,  974,  969,  965,  956,  906,
 /*    40 */   899,  854,  810,  806,  796,  728,  718,  714,  711,  645,
 /*    50 */   641,  570,   15,  518,  447,  577,  386,  369,  -90,  306,
 /*    60 */   305,   96,  -33,  416,  274,  581,  580,  574,  524,  552,
 /*    70 */   523,  527,  390,   93,  455,  450,  379,  578,  522,  519,
 /*    80 */   512,  507,  135,  365,  567,  567,  567,  567,  567,  567,
 /*    90 */   567,  567,  567,  567,  567,  567,  567,  567,  567,  567,
 /*   100 */   567,  567,  567,  567,  567,  567,  567,  567,  567,  567,
 /*   110 */   567,  567,  567,  567,  567,  567,  567,  567,  736,  567,
 /*   120 */   567,  766,  762,  757,  761,  758,  753,  743,  741,  754,
 /*   130 */   752,  747,  744,  741,  739,  731,  740,  706,  567,  567,
 /*   140 */   695,  715,  709,  725,  703,  680,  676,  675,  674,  673,
 /*   150 */   671,  670,  669,  666,  636,  632,  626,  639,  637,  593,
 /*   160 */   631,  594,  567,  567,  567,  567,  590,  592,  588,  587,
 /*   170 */   558,  557,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */   557,  493,  493,  493,  557,  557,  557,  493,  493,  493,
 /*    10 */   557,  557,  532,  557,  557,  557,  557,  557,  557,  557,
 /*    20 */   557,  557,  557,  557,  557,  557,  557,  557,  557,  557,
 /*    30 */   557,  557,  557,  557,  557,  557,  557,  557,  557,  557,
 /*    40 */   557,  557,  557,  557,  557,  557,  557,  557,  557,  557,
 /*    50 */   557,  557,  403,  557,  403,  557,  403,  557,  557,  557,
 /*    60 */   557,  449,  449,  486,  367,  403,  557,  557,  557,  557,
 /*    70 */   557,  403,  403,  403,  557,  557,  557,  557,  557,  557,
 /*    80 */   557,  557,  459,  478,  440,  557,  557,  557,  557,  431,
 /*    90 */   430,  490,  461,  492,  491,  451,  442,  441,  534,  535,
 /*   100 */   533,  531,  495,  557,  557,  494,  428,  512,  521,  520,
 /*   110 */   511,  524,  517,  516,  515,  519,  514,  518,  455,  508,
 /*   120 */   505,  557,  557,  557,  557,  397,  557,  397,  557,  557,
 /*   130 */   557,  557,  557,  557,  557,  427,  373,  486,  506,  507,
 /*   140 */   536,  454,  487,  557,  418,  427,  415,  422,  418,  395,
 /*   150 */   383,  395,  557,  557,  486,  458,  462,  439,  443,  450,
 /*   160 */   452,  557,  522,  510,  509,  513,  427,  436,  436,  436,
 /*   170 */   546,  546,  557,  557,  557,  557,  557,  557,  557,  557,
 /*   180 */   525,  557,  557,  417,  557,  557,  557,  557,  557,  388,
 /*   190 */   387,  557,  557,  557,  557,  557,  557,  557,  557,  557,
 /*   200 */   557,  557,  557,  557,  557,  557,  557,  557,  416,  557,
 /*   210 */   557,  557,  557,  381,  557,  557,  557,  557,  433,  489,
 /*   220 */   557,  557,  557,  557,  557,  557,  557,  453,  557,  444,
 /*   230 */   557,  557,  557,  557,  557,  557,  557,  557,  555,  554,
 /*   240 */   499,  497,  555,  554,  434,  557,  557,  429,  426,  419,
 /*   250 */   423,  421,  420,  412,  411,  410,  414,  413,  386,  385,
 /*   260 */   390,  389,  394,  393,  392,  391,  384,  382,  380,  379,
 /*   270 */   396,  378,  377,  376,  370,  369,  404,  402,  401,  400,
 /*   280 */   399,  374,  398,  409,  408,  407,  406,  405,  375,  368,
 /*   290 */   363,  437,  488,  480,  479,  477,  476,  485,  484,  475,
 /*   300 */   474,  425,  457,  424,  456,  473,  472,  471,  470,  469,
 /*   310 */   468,  467,  466,  465,  464,  463,  460,  446,  448,  447,
 /*   320 */   445,  438,  525,  502,  500,  528,  529,  538,  545,  543,
 /*   330 */   542,  541,  540,  539,  530,  537,  526,  527,  523,  503,
 /*   340 */   483,  482,  481,  501,  498,  550,  549,  548,  547,  544,
 /*   350 */   496,  556,  553,  552,  551,  504,  435,  432,  364,  366,
 /*   360 */   365,
};

/* The next table maps tokens into fallback tokens.  If a construct
** like the following:
** 
**      %fallback ID X Y Z.
**
** appears in the grammar, then ID becomes a fallback token for X, Y,
** and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
** but it does not parse, the type of the token is changed to ID and
** the parse is retried before an error is thrown.
*/
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
    0,  /*          $ => nothing */
    0,  /*         ID => nothing */
    1,  /*      ABORT => ID */
    1,  /*      AFTER => ID */
    1,  /*    ANALYZE => ID */
    1,  /*        ASC => ID */
    1,  /*     ATTACH => ID */
    1,  /*     BEFORE => ID */
    1,  /*      BEGIN => ID */
    1,  /*    CASCADE => ID */
    1,  /*       CAST => ID */
    1,  /*   CONFLICT => ID */
    1,  /*   DATABASE => ID */
    1,  /*   DEFERRED => ID */
    1,  /*       DESC => ID */
    1,  /*     DETACH => ID */
    1,  /*       EACH => ID */
    1,  /*        END => ID */
    1,  /*  EXCLUSIVE => ID */
    1,  /*    EXPLAIN => ID */
    1,  /*       FAIL => ID */
    1,  /*        FOR => ID */
    1,  /*     IGNORE => ID */
    1,  /*  IMMEDIATE => ID */
    1,  /*  INITIALLY => ID */
    1,  /*    INSTEAD => ID */
    1,  /*       LIKE => ID */
    1,  /*      MATCH => ID */
    1,  /*       PLAN => ID */
    1,  /*      QUERY => ID */
    1,  /*        KEY => ID */
    1,  /*         OF => ID */
    1,  /*     OFFSET => ID */
    1,  /*     PRAGMA => ID */
    1,  /*      RAISE => ID */
    1,  /*    REPLACE => ID */
    1,  /*   RESTRICT => ID */
    1,  /*        ROW => ID */
    1,  /*       TEMP => ID */
    1,  /*    TRIGGER => ID */
    1,  /*     VACUUM => ID */
    1,  /*       VIEW => ID */
    1,  /*    VIRTUAL => ID */
    1,  /*    REINDEX => ID */
    1,  /*     RENAME => ID */
    1,  /*   CTIME_KW => ID */
    1,  /*         IF => ID */
    1,  /*  DELIMITER => ID */
    1,  /*     COMMIT => ID */
    1,  /*   ROLLBACK => ID */
    1,  /*  ISOLATION => ID */
    1,  /*      LEVEL => ID */
    1,  /* SERIALIZABLE => ID */
    1,  /*       READ => ID */
    1,  /*  COMMITTED => ID */
    1,  /* UNCOMMITTED => ID */
    1,  /* REPEATABLE => ID */
    1,  /*      WRITE => ID */
    1,  /*       ONLY => ID */
    1,  /*  SAVEPOINT => ID */
    1,  /*    RELEASE => ID */
    1,  /*    COMMENT => ID */
    1,  /*      FORCE => ID */
    1,  /*       WAIT => ID */
    1,  /*     NOWAIT => ID */
    1,  /*      BATCH => ID */
};
#endif /* YYFALLBACK */

/* The following structure represents a single element of the
** parser's stack.  Information stored includes:
**
**   +  The state number for the parser at this level of the stack.
**
**   +  The value of the token stored at this level of the stack.
**      (In other words, the "major" token.)
**
**   +  The semantic value stored at this level of the stack.  This is
**      the information used by the action routines in the grammar.
**      It is sometimes called the "minor" token.
*/
struct yyStackEntry {
  YYACTIONTYPE stateno;  /* The state-number */
  YYCODETYPE major;      /* The major token value.  This is the code
                         ** number for the token at this stack level */
  YYMINORTYPE minor;     /* The user-supplied minor token value.  This
                         ** is the value of the token  */
};
typedef struct yyStackEntry yyStackEntry;

/* The state of the parser is completely contained in an instance of
** the following structure */
struct yyParser {
  int yyidx;                    /* Index of top element in stack */
#ifdef YYTRACKMAXSTACKDEPTH
  int yyidxMax;                 /* Maximum value of yyidx */
#endif
  int yyerrcnt;                 /* Shifts left before out of the error */
  gda_lemon_oracle_parserARG_SDECL                /* A place to hold %extra_argument */
#if YYSTACKDEPTH<=0
  int yystksz;                  /* Current side of the stack */
  yyStackEntry *yystack;        /* The parser's stack */
#else
  yyStackEntry yystack[YYSTACKDEPTH];  /* The parser's stack */
#endif
};
typedef struct yyParser yyParser;

#ifndef NDEBUG
#include <stdio.h>
static FILE *yyTraceFILE = 0;
static char *yyTracePrompt = 0;
#endif /* NDEBUG */

#ifndef NDEBUG
/* 
** Turn parser tracing on by giving a stream to which to write the trace
** and a prompt to preface each trace message.  Tracing is turned off
** by making either argument NULL 
**
** Inputs:
** <ul>
** <li> A FILE* to which trace output should be written.
**      If NULL, then tracing is turned off.
** <li> A prefix string written at the beginning of every
**      line of trace output.  If NULL, then tracing is
**      turned off.
** </ul>
**
** Outputs:
** None.
*/
void gda_lemon_oracle_parserTrace(FILE *TraceFILE, char *zTracePrompt){
  yyTraceFILE = TraceFILE;
  yyTracePrompt = zTracePrompt;
  if( yyTraceFILE==0 ) yyTracePrompt = 0;
  else if( yyTracePrompt==0 ) yyTraceFILE = 0;
}
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing shifts, the names of all terminals and nonterminals
** are required.  The following table supplies these names */
static const char *const yyTokenName[] = { 
  "$",             "ID",            "ABORT",         "AFTER",       
  "ANALYZE",       "ASC",           "ATTACH",        "BEFORE",      
  "BEGIN",         "CASCADE",       "CAST",          "CONFLICT",    
  "DATABASE",      "DEFERRED",      "DESC",          "DETACH",      
  "EACH",          "END",           "EXCLUSIVE",     "EXPLAIN",     
  "FAIL",          "FOR",           "IGNORE",        "IMMEDIATE",   
  "INITIALLY",     "INSTEAD",       "LIKE",          "MATCH",       
  "PLAN",          "QUERY",         "KEY",           "OF",          
  "OFFSET",        "PRAGMA",        "RAISE",         "REPLACE",     
  "RESTRICT",      "ROW",           "TEMP",          "TRIGGER",     
  "VACUUM",        "VIEW",          "VIRTUAL",       "REINDEX",     
  "RENAME",        "CTIME_KW",      "IF",            "DELIMITER",   
  "COMMIT",        "ROLLBACK",      "ISOLATION",     "LEVEL",       
  "SERIALIZABLE",  "READ",          "COMMITTED",     "UNCOMMITTED", 
  "REPEATABLE",    "WRITE",         "ONLY",          "SAVEPOINT",   
  "RELEASE",       "COMMENT",       "FORCE",         "WAIT",        
  "NOWAIT",        "BATCH",         "OR",            "AND",         
  "NOT",           "IS",            "NOTLIKE",       "IN",          
  "ISNULL",        "NOTNULL",       "DIFF",          "EQ",          
  "BETWEEN",       "GT",            "LEQ",           "LT",          
  "GEQ",           "REGEXP",        "REGEXP_CI",     "NOT_REGEXP",  
  "NOT_REGEXP_CI",  "SIMILAR",       "ESCAPE",        "BITAND",      
  "BITOR",         "LSHIFT",        "RSHIFT",        "PLUS",        
  "MINUS",         "STAR",          "SLASH",         "REM",         
  "CONCAT",        "COLLATE",       "UMINUS",        "UPLUS",       
  "BITNOT",        "LP",            "RP",            "JOIN",        
  "INNER",         "NATURAL",       "LEFT",          "RIGHT",       
  "FULL",          "CROSS",         "UNION",         "EXCEPT",      
  "INTERSECT",     "PGCAST",        "ILLEGAL",       "SQLCOMMENT",  
  "SEMI",          "END_OF_FILE",   "TRANSACTION",   "STRING",      
  "COMMA",         "INTEGER",       "TO",            "INSERT",      
  "INTO",          "VALUES",        "DELETE",        "FROM",        
  "WHERE",         "UPDATE",        "SET",           "ALL",         
  "SELECT",        "LIMIT",         "ORDER",         "BY",          
  "HAVING",        "GROUP",         "USING",         "ON",          
  "OUTER",         "DOT",           "AS",            "DISTINCT",    
  "CASE",          "WHEN",          "THEN",          "ELSE",        
  "NULL",          "FLOAT",         "UNSPECVAL",     "LSBRACKET",   
  "RSBRACKET",     "SIMPLEPARAM",   "PNAME",         "PDESCR",      
  "PTYPE",         "PNULLOK",       "TEXTUAL",       "error",       
  "stmt",          "cmd",           "eos",           "compound",    
  "nm_opt",        "transtype",     "transilev",     "opt_comma",   
  "trans_opt_kw",  "ora_commit_write",  "nm",            "opt_on_conflict",
  "fullname",      "inscollist_opt",  "rexprlist",     "ins_extra_values",
  "inscollist",    "where_opt",     "expr",          "setlist",     
  "selectcmd",     "opt_compound_all",  "distinct",      "selcollist",  
  "from",          "groupby_opt",   "having_opt",    "orderby_opt", 
  "limit_opt",     "sortlist",      "sortorder",     "rnexprlist",  
  "seltablist",    "stl_prefix",    "seltarget",     "on_cond",     
  "using_opt",     "jointype",      "as",            "sclp",        
  "starname",      "value",         "pvalue",        "uni_op",      
  "case_operand",  "case_exprlist",  "case_else",     "paramspec",   
};
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing reduce actions, the names of all rules are required.
*/
static const char *const yyRuleName[] = {
 /*   0 */ "stmt ::= cmd eos",
 /*   1 */ "stmt ::= compound eos",
 /*   2 */ "cmd ::= LP cmd RP",
 /*   3 */ "compound ::= LP compound RP",
 /*   4 */ "eos ::= SEMI",
 /*   5 */ "eos ::= END_OF_FILE",
 /*   6 */ "cmd ::= BEGIN",
 /*   7 */ "cmd ::= BEGIN TRANSACTION nm_opt",
 /*   8 */ "cmd ::= BEGIN transtype TRANSACTION nm_opt",
 /*   9 */ "cmd ::= BEGIN transtype nm_opt",
 /*  10 */ "cmd ::= BEGIN transilev",
 /*  11 */ "cmd ::= BEGIN TRANSACTION transilev",
 /*  12 */ "cmd ::= BEGIN TRANSACTION transtype",
 /*  13 */ "cmd ::= BEGIN TRANSACTION transtype opt_comma transilev",
 /*  14 */ "cmd ::= BEGIN TRANSACTION transilev opt_comma transtype",
 /*  15 */ "cmd ::= BEGIN transtype opt_comma transilev",
 /*  16 */ "cmd ::= BEGIN transilev opt_comma transtype",
 /*  17 */ "cmd ::= END trans_opt_kw nm_opt",
 /*  18 */ "cmd ::= COMMIT nm_opt",
 /*  19 */ "cmd ::= COMMIT TRANSACTION nm_opt",
 /*  20 */ "cmd ::= COMMIT FORCE STRING",
 /*  21 */ "cmd ::= COMMIT FORCE STRING COMMA INTEGER",
 /*  22 */ "cmd ::= COMMIT COMMENT STRING",
 /*  23 */ "cmd ::= COMMIT COMMENT STRING ora_commit_write",
 /*  24 */ "cmd ::= COMMIT ora_commit_write",
 /*  25 */ "cmd ::= ROLLBACK trans_opt_kw nm_opt",
 /*  26 */ "ora_commit_write ::= WRITE IMMEDIATE",
 /*  27 */ "ora_commit_write ::= WRITE BATCH",
 /*  28 */ "ora_commit_write ::= WRITE WAIT",
 /*  29 */ "ora_commit_write ::= WRITE NOWAIT",
 /*  30 */ "ora_commit_write ::= WRITE IMMEDIATE WAIT",
 /*  31 */ "ora_commit_write ::= WRITE IMMEDIATE NOWAIT",
 /*  32 */ "ora_commit_write ::= WRITE BATCH WAIT",
 /*  33 */ "ora_commit_write ::= WRITE BATCH NOWAIT",
 /*  34 */ "trans_opt_kw ::=",
 /*  35 */ "trans_opt_kw ::= TRANSACTION",
 /*  36 */ "opt_comma ::=",
 /*  37 */ "opt_comma ::= COMMA",
 /*  38 */ "transilev ::= ISOLATION LEVEL SERIALIZABLE",
 /*  39 */ "transilev ::= ISOLATION LEVEL REPEATABLE READ",
 /*  40 */ "transilev ::= ISOLATION LEVEL READ COMMITTED",
 /*  41 */ "transilev ::= ISOLATION LEVEL READ UNCOMMITTED",
 /*  42 */ "nm_opt ::=",
 /*  43 */ "nm_opt ::= nm",
 /*  44 */ "transtype ::= DEFERRED",
 /*  45 */ "transtype ::= IMMEDIATE",
 /*  46 */ "transtype ::= EXCLUSIVE",
 /*  47 */ "transtype ::= READ WRITE",
 /*  48 */ "transtype ::= READ ONLY",
 /*  49 */ "cmd ::= SAVEPOINT nm",
 /*  50 */ "cmd ::= RELEASE SAVEPOINT nm",
 /*  51 */ "cmd ::= RELEASE nm",
 /*  52 */ "cmd ::= ROLLBACK trans_opt_kw TO nm",
 /*  53 */ "cmd ::= ROLLBACK trans_opt_kw TO SAVEPOINT nm",
 /*  54 */ "cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP",
 /*  55 */ "cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP ins_extra_values",
 /*  56 */ "cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt compound",
 /*  57 */ "opt_on_conflict ::=",
 /*  58 */ "opt_on_conflict ::= OR ID",
 /*  59 */ "ins_extra_values ::= ins_extra_values COMMA LP rexprlist RP",
 /*  60 */ "ins_extra_values ::= COMMA LP rexprlist RP",
 /*  61 */ "inscollist_opt ::=",
 /*  62 */ "inscollist_opt ::= LP inscollist RP",
 /*  63 */ "inscollist ::= inscollist COMMA fullname",
 /*  64 */ "inscollist ::= fullname",
 /*  65 */ "cmd ::= DELETE FROM fullname where_opt",
 /*  66 */ "where_opt ::=",
 /*  67 */ "where_opt ::= WHERE expr",
 /*  68 */ "cmd ::= UPDATE opt_on_conflict fullname SET setlist where_opt",
 /*  69 */ "setlist ::= setlist COMMA fullname EQ expr",
 /*  70 */ "setlist ::= fullname EQ expr",
 /*  71 */ "compound ::= selectcmd",
 /*  72 */ "compound ::= compound UNION opt_compound_all compound",
 /*  73 */ "compound ::= compound EXCEPT opt_compound_all compound",
 /*  74 */ "compound ::= compound INTERSECT opt_compound_all compound",
 /*  75 */ "opt_compound_all ::=",
 /*  76 */ "opt_compound_all ::= ALL",
 /*  77 */ "selectcmd ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /*  78 */ "limit_opt ::=",
 /*  79 */ "limit_opt ::= LIMIT expr",
 /*  80 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /*  81 */ "limit_opt ::= LIMIT expr COMMA expr",
 /*  82 */ "orderby_opt ::=",
 /*  83 */ "orderby_opt ::= ORDER BY sortlist",
 /*  84 */ "sortlist ::= sortlist COMMA expr sortorder",
 /*  85 */ "sortlist ::= expr sortorder",
 /*  86 */ "sortorder ::= ASC",
 /*  87 */ "sortorder ::= DESC",
 /*  88 */ "sortorder ::=",
 /*  89 */ "having_opt ::=",
 /*  90 */ "having_opt ::= HAVING expr",
 /*  91 */ "groupby_opt ::=",
 /*  92 */ "groupby_opt ::= GROUP BY rnexprlist",
 /*  93 */ "from ::=",
 /*  94 */ "from ::= FROM seltablist",
 /*  95 */ "seltablist ::= stl_prefix seltarget on_cond using_opt",
 /*  96 */ "using_opt ::= USING LP inscollist RP",
 /*  97 */ "using_opt ::=",
 /*  98 */ "stl_prefix ::=",
 /*  99 */ "stl_prefix ::= seltablist jointype",
 /* 100 */ "on_cond ::= ON expr",
 /* 101 */ "on_cond ::=",
 /* 102 */ "jointype ::= COMMA",
 /* 103 */ "jointype ::= JOIN",
 /* 104 */ "jointype ::= CROSS JOIN",
 /* 105 */ "jointype ::= INNER JOIN",
 /* 106 */ "jointype ::= NATURAL JOIN",
 /* 107 */ "jointype ::= LEFT JOIN",
 /* 108 */ "jointype ::= LEFT OUTER JOIN",
 /* 109 */ "jointype ::= RIGHT JOIN",
 /* 110 */ "jointype ::= RIGHT OUTER JOIN",
 /* 111 */ "jointype ::= FULL JOIN",
 /* 112 */ "jointype ::= FULL OUTER JOIN",
 /* 113 */ "seltarget ::= fullname as",
 /* 114 */ "seltarget ::= fullname ID",
 /* 115 */ "seltarget ::= LP compound RP as",
 /* 116 */ "sclp ::= selcollist COMMA",
 /* 117 */ "sclp ::=",
 /* 118 */ "selcollist ::= sclp expr as",
 /* 119 */ "selcollist ::= sclp starname",
 /* 120 */ "starname ::= STAR",
 /* 121 */ "starname ::= nm DOT STAR",
 /* 122 */ "starname ::= nm DOT nm DOT STAR",
 /* 123 */ "as ::= AS fullname",
 /* 124 */ "as ::= AS value",
 /* 125 */ "as ::=",
 /* 126 */ "distinct ::=",
 /* 127 */ "distinct ::= ALL",
 /* 128 */ "distinct ::= DISTINCT",
 /* 129 */ "distinct ::= DISTINCT ON expr",
 /* 130 */ "rnexprlist ::= rnexprlist COMMA expr",
 /* 131 */ "rnexprlist ::= expr",
 /* 132 */ "rexprlist ::=",
 /* 133 */ "rexprlist ::= rexprlist COMMA expr",
 /* 134 */ "rexprlist ::= expr",
 /* 135 */ "expr ::= pvalue",
 /* 136 */ "expr ::= value",
 /* 137 */ "expr ::= LP expr RP",
 /* 138 */ "expr ::= fullname",
 /* 139 */ "expr ::= fullname LP rexprlist RP",
 /* 140 */ "expr ::= fullname LP compound RP",
 /* 141 */ "expr ::= fullname LP starname RP",
 /* 142 */ "expr ::= CAST LP expr AS fullname RP",
 /* 143 */ "expr ::= expr PGCAST fullname",
 /* 144 */ "expr ::= expr PLUS|MINUS expr",
 /* 145 */ "expr ::= expr STAR expr",
 /* 146 */ "expr ::= expr SLASH|REM expr",
 /* 147 */ "expr ::= expr BITAND|BITOR expr",
 /* 148 */ "expr ::= MINUS expr",
 /* 149 */ "expr ::= PLUS expr",
 /* 150 */ "expr ::= expr AND expr",
 /* 151 */ "expr ::= expr OR expr",
 /* 152 */ "expr ::= expr CONCAT expr",
 /* 153 */ "expr ::= expr GT|LEQ|GEQ|LT expr",
 /* 154 */ "expr ::= expr DIFF|EQ expr",
 /* 155 */ "expr ::= expr LIKE expr",
 /* 156 */ "expr ::= expr NOTLIKE expr",
 /* 157 */ "expr ::= expr REGEXP|REGEXP_CI|NOT_REGEXP|NOT_REGEXP_CI|SIMILAR expr",
 /* 158 */ "expr ::= expr BETWEEN expr AND expr",
 /* 159 */ "expr ::= expr NOT BETWEEN expr AND expr",
 /* 160 */ "expr ::= NOT expr",
 /* 161 */ "expr ::= BITNOT expr",
 /* 162 */ "expr ::= expr uni_op",
 /* 163 */ "expr ::= expr IS expr",
 /* 164 */ "expr ::= LP compound RP",
 /* 165 */ "expr ::= expr IN LP rexprlist RP",
 /* 166 */ "expr ::= expr IN LP compound RP",
 /* 167 */ "expr ::= expr NOT IN LP rexprlist RP",
 /* 168 */ "expr ::= expr NOT IN LP compound RP",
 /* 169 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 170 */ "case_operand ::= expr",
 /* 171 */ "case_operand ::=",
 /* 172 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 173 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 174 */ "case_else ::= ELSE expr",
 /* 175 */ "case_else ::=",
 /* 176 */ "uni_op ::= ISNULL",
 /* 177 */ "uni_op ::= IS NOTNULL",
 /* 178 */ "value ::= NULL",
 /* 179 */ "value ::= STRING",
 /* 180 */ "value ::= INTEGER",
 /* 181 */ "value ::= FLOAT",
 /* 182 */ "pvalue ::= UNSPECVAL LSBRACKET paramspec RSBRACKET",
 /* 183 */ "pvalue ::= value LSBRACKET paramspec RSBRACKET",
 /* 184 */ "pvalue ::= SIMPLEPARAM",
 /* 185 */ "paramspec ::=",
 /* 186 */ "paramspec ::= paramspec PNAME",
 /* 187 */ "paramspec ::= paramspec PDESCR",
 /* 188 */ "paramspec ::= paramspec PTYPE",
 /* 189 */ "paramspec ::= paramspec PNULLOK",
 /* 190 */ "nm ::= JOIN",
 /* 191 */ "nm ::= ID",
 /* 192 */ "nm ::= TEXTUAL",
 /* 193 */ "fullname ::= nm",
 /* 194 */ "fullname ::= nm DOT nm",
 /* 195 */ "fullname ::= nm DOT nm DOT nm",
};
#endif /* NDEBUG */


#if YYSTACKDEPTH<=0
/*
** Try to increase the size of the parser stack.
*/
static void yyGrowStack(yyParser *p){
  int newSize;
  yyStackEntry *pNew;

  newSize = p->yystksz*2 + 100;
  pNew = realloc(p->yystack, newSize*sizeof(pNew[0]));
  if( pNew ){
    p->yystack = pNew;
    p->yystksz = newSize;
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,"%sStack grows to %d entries!\n",
              yyTracePrompt, p->yystksz);
    }
#endif
  }
}
#endif

/* 
** This function allocates a new parser.
** The only argument is a pointer to a function which works like
** malloc.
**
** Inputs:
** A pointer to the function used to allocate memory.
**
** Outputs:
** A pointer to a parser.  This pointer is used in subsequent calls
** to gda_lemon_oracle_parser and gda_lemon_oracle_parserFree.
*/
void *gda_lemon_oracle_parserAlloc(void *(*mallocProc)(size_t)){
  yyParser *pParser;
  pParser = (yyParser*)(*mallocProc)( (size_t)sizeof(yyParser) );
  if( pParser ){
    pParser->yyidx = -1;
#ifdef YYTRACKMAXSTACKDEPTH
    pParser->yyidxMax = 0;
#endif
#if YYSTACKDEPTH<=0
    pParser->yystack = NULL;
    pParser->yystksz = 0;
    yyGrowStack(pParser);
#endif
  }
  return pParser;
}

/* The following function deletes the value associated with a
** symbol.  The symbol can be either a terminal or nonterminal.
** "yymajor" is the symbol code, and "yypminor" is a pointer to
** the value.
*/
static void yy_destructor(
  yyParser *yypParser,    /* The parser */
  YYCODETYPE yymajor,     /* Type code for object to destroy */
  YYMINORTYPE *yypminor   /* The object to be destroyed */
){
  gda_lemon_oracle_parserARG_FETCH;
  switch( yymajor ){
    /* Here is inserted the actions which take place when a
    ** terminal or non-terminal is destroyed.  This can happen
    ** when the symbol is popped from the stack during a
    ** reduce or during error processing or when a parser is 
    ** being destroyed before it is finished parsing.
    **
    ** Note: during a reduce, the only symbols destroyed are those
    ** which appear on the RHS of the rule, but which are not used
    ** inside the C code.
    */
      /* TERMINAL Destructor */
    case 1: /* ID */
    case 2: /* ABORT */
    case 3: /* AFTER */
    case 4: /* ANALYZE */
    case 5: /* ASC */
    case 6: /* ATTACH */
    case 7: /* BEFORE */
    case 8: /* BEGIN */
    case 9: /* CASCADE */
    case 10: /* CAST */
    case 11: /* CONFLICT */
    case 12: /* DATABASE */
    case 13: /* DEFERRED */
    case 14: /* DESC */
    case 15: /* DETACH */
    case 16: /* EACH */
    case 17: /* END */
    case 18: /* EXCLUSIVE */
    case 19: /* EXPLAIN */
    case 20: /* FAIL */
    case 21: /* FOR */
    case 22: /* IGNORE */
    case 23: /* IMMEDIATE */
    case 24: /* INITIALLY */
    case 25: /* INSTEAD */
    case 26: /* LIKE */
    case 27: /* MATCH */
    case 28: /* PLAN */
    case 29: /* QUERY */
    case 30: /* KEY */
    case 31: /* OF */
    case 32: /* OFFSET */
    case 33: /* PRAGMA */
    case 34: /* RAISE */
    case 35: /* REPLACE */
    case 36: /* RESTRICT */
    case 37: /* ROW */
    case 38: /* TEMP */
    case 39: /* TRIGGER */
    case 40: /* VACUUM */
    case 41: /* VIEW */
    case 42: /* VIRTUAL */
    case 43: /* REINDEX */
    case 44: /* RENAME */
    case 45: /* CTIME_KW */
    case 46: /* IF */
    case 47: /* DELIMITER */
    case 48: /* COMMIT */
    case 49: /* ROLLBACK */
    case 50: /* ISOLATION */
    case 51: /* LEVEL */
    case 52: /* SERIALIZABLE */
    case 53: /* READ */
    case 54: /* COMMITTED */
    case 55: /* UNCOMMITTED */
    case 56: /* REPEATABLE */
    case 57: /* WRITE */
    case 58: /* ONLY */
    case 59: /* SAVEPOINT */
    case 60: /* RELEASE */
    case 61: /* COMMENT */
    case 62: /* FORCE */
    case 63: /* WAIT */
    case 64: /* NOWAIT */
    case 65: /* BATCH */
    case 66: /* OR */
    case 67: /* AND */
    case 68: /* NOT */
    case 69: /* IS */
    case 70: /* NOTLIKE */
    case 71: /* IN */
    case 72: /* ISNULL */
    case 73: /* NOTNULL */
    case 74: /* DIFF */
    case 75: /* EQ */
    case 76: /* BETWEEN */
    case 77: /* GT */
    case 78: /* LEQ */
    case 79: /* LT */
    case 80: /* GEQ */
    case 81: /* REGEXP */
    case 82: /* REGEXP_CI */
    case 83: /* NOT_REGEXP */
    case 84: /* NOT_REGEXP_CI */
    case 85: /* SIMILAR */
    case 86: /* ESCAPE */
    case 87: /* BITAND */
    case 88: /* BITOR */
    case 89: /* LSHIFT */
    case 90: /* RSHIFT */
    case 91: /* PLUS */
    case 92: /* MINUS */
    case 93: /* STAR */
    case 94: /* SLASH */
    case 95: /* REM */
    case 96: /* CONCAT */
    case 97: /* COLLATE */
    case 98: /* UMINUS */
    case 99: /* UPLUS */
    case 100: /* BITNOT */
    case 101: /* LP */
    case 102: /* RP */
    case 103: /* JOIN */
    case 104: /* INNER */
    case 105: /* NATURAL */
    case 106: /* LEFT */
    case 107: /* RIGHT */
    case 108: /* FULL */
    case 109: /* CROSS */
    case 110: /* UNION */
    case 111: /* EXCEPT */
    case 112: /* INTERSECT */
    case 113: /* PGCAST */
    case 114: /* ILLEGAL */
    case 115: /* SQLCOMMENT */
    case 116: /* SEMI */
    case 117: /* END_OF_FILE */
    case 118: /* TRANSACTION */
    case 119: /* STRING */
    case 120: /* COMMA */
    case 121: /* INTEGER */
    case 122: /* TO */
    case 123: /* INSERT */
    case 124: /* INTO */
    case 125: /* VALUES */
    case 126: /* DELETE */
    case 127: /* FROM */
    case 128: /* WHERE */
    case 129: /* UPDATE */
    case 130: /* SET */
    case 131: /* ALL */
    case 132: /* SELECT */
    case 133: /* LIMIT */
    case 134: /* ORDER */
    case 135: /* BY */
    case 136: /* HAVING */
    case 137: /* GROUP */
    case 138: /* USING */
    case 139: /* ON */
    case 140: /* OUTER */
    case 141: /* DOT */
    case 142: /* AS */
    case 143: /* DISTINCT */
    case 144: /* CASE */
    case 145: /* WHEN */
    case 146: /* THEN */
    case 147: /* ELSE */
    case 148: /* NULL */
    case 149: /* FLOAT */
    case 150: /* UNSPECVAL */
    case 151: /* LSBRACKET */
    case 152: /* RSBRACKET */
    case 153: /* SIMPLEPARAM */
    case 154: /* PNAME */
    case 155: /* PDESCR */
    case 156: /* PTYPE */
    case 157: /* PNULLOK */
    case 158: /* TEXTUAL */
{
#line 9 "./parser.y"
if ((yypminor->yy0)) {
#ifdef GDA_DEBUG_NO
		 gchar *str = gda_sql_value_stringify ((yypminor->yy0));
		 g_print ("___ token destructor /%s/\n", str)
		 g_free (str);
#endif
		 g_value_unset ((yypminor->yy0)); g_free ((yypminor->yy0));}
#line 1394 "parser.c"
}
      break;
    case 160: /* stmt */
{
#line 278 "./parser.y"
g_print ("Statement destroyed by parser: %p\n", (yypminor->yy252)); gda_sql_statement_free ((yypminor->yy252));
#line 1401 "parser.c"
}
      break;
    case 161: /* cmd */
    case 163: /* compound */
    case 180: /* selectcmd */
{
#line 301 "./parser.y"
gda_sql_statement_free ((yypminor->yy252));
#line 1410 "parser.c"
}
      break;
    case 173: /* inscollist_opt */
    case 176: /* inscollist */
    case 196: /* using_opt */
{
#line 478 "./parser.y"
if ((yypminor->yy105)) {g_slist_foreach ((yypminor->yy105), (GFunc) gda_sql_field_free, NULL); g_slist_free ((yypminor->yy105));}
#line 1419 "parser.c"
}
      break;
    case 174: /* rexprlist */
    case 191: /* rnexprlist */
{
#line 764 "./parser.y"
if ((yypminor->yy33)) {g_slist_foreach ((yypminor->yy33), (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy33));}
#line 1427 "parser.c"
}
      break;
    case 175: /* ins_extra_values */
{
#line 467 "./parser.y"
GSList *list;
		for (list = (yypminor->yy105); list; list = list->next) {
			g_slist_foreach ((GSList*) list->data, (GFunc) gda_sql_field_free, NULL); 
			g_slist_free ((GSList*) list->data);
		}
		g_slist_free ((yypminor->yy105));

#line 1440 "parser.c"
}
      break;
    case 177: /* where_opt */
    case 178: /* expr */
    case 186: /* having_opt */
    case 195: /* on_cond */
    case 202: /* pvalue */
{
#line 500 "./parser.y"
gda_sql_expr_free ((yypminor->yy302));
#line 1451 "parser.c"
}
      break;
    case 179: /* setlist */
{
#line 520 "./parser.y"
GSList *list;
	for (list = (yypminor->yy105); list; list = list->next) {
		UpdateSet *set = (UpdateSet*) list->data;
		g_value_reset (set->fname); g_free (set->fname);
		gda_sql_expr_free (set->expr);
		g_free (set);
	}
	g_slist_free ((yypminor->yy105));

#line 1466 "parser.c"
}
      break;
    case 182: /* distinct */
{
#line 750 "./parser.y"
if ((yypminor->yy249)) {if ((yypminor->yy249)->expr) gda_sql_expr_free ((yypminor->yy249)->expr); g_free ((yypminor->yy249));}
#line 1473 "parser.c"
}
      break;
    case 183: /* selcollist */
    case 199: /* sclp */
{
#line 707 "./parser.y"
g_slist_foreach ((yypminor->yy33), (GFunc) gda_sql_select_field_free, NULL); g_slist_free ((yypminor->yy33));
#line 1481 "parser.c"
}
      break;
    case 184: /* from */
    case 192: /* seltablist */
    case 193: /* stl_prefix */
{
#line 633 "./parser.y"
gda_sql_select_from_free ((yypminor->yy259));
#line 1490 "parser.c"
}
      break;
    case 185: /* groupby_opt */
{
#line 628 "./parser.y"
if ((yypminor->yy105)) {g_slist_foreach ((yypminor->yy105), (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy105));}
#line 1497 "parser.c"
}
      break;
    case 187: /* orderby_opt */
    case 189: /* sortlist */
{
#line 597 "./parser.y"
if ((yypminor->yy33)) {g_slist_foreach ((yypminor->yy33), (GFunc) gda_sql_select_order_free, NULL); g_slist_free ((yypminor->yy33));}
#line 1505 "parser.c"
}
      break;
    case 188: /* limit_opt */
{
#line 590 "./parser.y"
gda_sql_expr_free ((yypminor->yy408).count); gda_sql_expr_free ((yypminor->yy408).offset);
#line 1512 "parser.c"
}
      break;
    case 194: /* seltarget */
{
#line 692 "./parser.y"
gda_sql_select_target_free ((yypminor->yy414));
#line 1519 "parser.c"
}
      break;
    case 204: /* case_operand */
    case 206: /* case_else */
{
#line 929 "./parser.y"
gda_sql_expr_free ((yypminor->yy354));
#line 1527 "parser.c"
}
      break;
    case 205: /* case_exprlist */
{
#line 934 "./parser.y"
g_slist_foreach ((yypminor->yy51).when_list, (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy51).when_list);
	g_slist_foreach ((yypminor->yy51).then_list, (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy51).then_list);
#line 1535 "parser.c"
}
      break;
    case 207: /* paramspec */
{
#line 972 "./parser.y"
gda_sql_param_spec_free ((yypminor->yy303));
#line 1542 "parser.c"
}
      break;
    default:  break;   /* If no destructor action specified: do nothing */
  }
}

/*
** Pop the parser's stack once.
**
** If there is a destructor routine associated with the token which
** is popped from the stack, then call it.
**
** Return the major token number for the symbol popped.
*/
static int yy_pop_parser_stack(yyParser *pParser){
  YYCODETYPE yymajor;
  yyStackEntry *yytos;

  if( pParser->yyidx<0 ) return 0;
  yytos = &pParser->yystack[pParser->yyidx];
#ifndef NDEBUG
  if( yyTraceFILE && pParser->yyidx>=0 ){
    fprintf(yyTraceFILE,"%sPopping %s\n",
      yyTracePrompt,
      yyTokenName[yytos->major]);
  }
#endif
  yymajor = yytos->major;
  yy_destructor(pParser, yymajor, &yytos->minor);
  pParser->yyidx--;
  return yymajor;
}

/* 
** Deallocate and destroy a parser.  Destructors are all called for
** all stack elements before shutting the parser down.
**
** Inputs:
** <ul>
** <li>  A pointer to the parser.  This should be a pointer
**       obtained from gda_lemon_oracle_parserAlloc.
** <li>  A pointer to a function used to reclaim memory obtained
**       from malloc.
** </ul>
*/
void gda_lemon_oracle_parserFree(
  void *p,                    /* The parser to be deleted */
  void (*freeProc)(void*)     /* Function used to reclaim memory */
){
  yyParser *pParser = (yyParser*)p;
  if( pParser==0 ) return;
  while( pParser->yyidx>=0 ) yy_pop_parser_stack(pParser);
#if YYSTACKDEPTH<=0
  free(pParser->yystack);
#endif
  (*freeProc)((void*)pParser);
}

/*
** Return the peak depth of the stack for a parser.
*/
#ifdef YYTRACKMAXSTACKDEPTH
int gda_lemon_oracle_parserStackPeak(void *p){
  yyParser *pParser = (yyParser*)p;
  return pParser->yyidxMax;
}
#endif

/*
** Find the appropriate action for a parser given the terminal
** look-ahead token iLookAhead.
**
** If the look-ahead token is YYNOCODE, then check to see if the action is
** independent of the look-ahead.  If it is, return the action, otherwise
** return YY_NO_ACTION.
*/
static int yy_find_shift_action(
  yyParser *pParser,        /* The parser */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
  int stateno = pParser->yystack[pParser->yyidx].stateno;
 
  if( stateno>YY_SHIFT_COUNT
   || (i = yy_shift_ofst[stateno])==YY_SHIFT_USE_DFLT ){
    return yy_default[stateno];
  }
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    if( iLookAhead>0 ){
#ifdef YYFALLBACK
      YYCODETYPE iFallback;            /* Fallback token */
      if( iLookAhead<sizeof(yyFallback)/sizeof(yyFallback[0])
             && (iFallback = yyFallback[iLookAhead])!=0 ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE, "%sFALLBACK %s => %s\n",
             yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[iFallback]);
        }
#endif
        return yy_find_shift_action(pParser, iFallback);
      }
#endif
#ifdef YYWILDCARD
      {
        int j = i - iLookAhead + YYWILDCARD;
        if( 
#if YY_SHIFT_MIN+YYWILDCARD<0
          j>=0 &&
#endif
#if YY_SHIFT_MAX+YYWILDCARD>=YY_ACTTAB_COUNT
          j<YY_ACTTAB_COUNT &&
#endif
          yy_lookahead[j]==YYWILDCARD
        ){
#ifndef NDEBUG
          if( yyTraceFILE ){
            fprintf(yyTraceFILE, "%sWILDCARD %s => %s\n",
               yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[YYWILDCARD]);
          }
#endif /* NDEBUG */
          return yy_action[j];
        }
      }
#endif /* YYWILDCARD */
    }
    return yy_default[stateno];
  }else{
    return yy_action[i];
  }
}

/*
** Find the appropriate action for a parser given the non-terminal
** look-ahead token iLookAhead.
**
** If the look-ahead token is YYNOCODE, then check to see if the action is
** independent of the look-ahead.  If it is, return the action, otherwise
** return YY_NO_ACTION.
*/
static int yy_find_reduce_action(
  int stateno,              /* Current state number */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
#ifdef YYERRORSYMBOL
  if( stateno>YY_REDUCE_COUNT ){
    return yy_default[stateno];
  }
#else
  assert( stateno<=YY_REDUCE_COUNT );
#endif
  i = yy_reduce_ofst[stateno];
  assert( i!=YY_REDUCE_USE_DFLT );
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
#ifdef YYERRORSYMBOL
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    return yy_default[stateno];
  }
#else
  assert( i>=0 && i<YY_ACTTAB_COUNT );
  assert( yy_lookahead[i]==iLookAhead );
#endif
  return yy_action[i];
}

/*
** The following routine is called if the stack overflows.
*/
static void yyStackOverflow(yyParser *yypParser, YYMINORTYPE *yypMinor){
   gda_lemon_oracle_parserARG_FETCH;
   yypParser->yyidx--;
#ifndef NDEBUG
   if( yyTraceFILE ){
     fprintf(yyTraceFILE,"%sStack Overflow!\n",yyTracePrompt);
   }
#endif
   while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
   /* Here code is inserted which will execute if the parser
   ** stack every overflows */
#line 25 "./parser.y"

	gda_sql_parser_set_overflow_error (pdata->parser);
#line 1728 "parser.c"
   gda_lemon_oracle_parserARG_STORE; /* Suppress warning about unused %extra_argument var */
}

/*
** Perform a shift action.
*/
static void yy_shift(
  yyParser *yypParser,          /* The parser to be shifted */
  int yyNewState,               /* The new state to shift in */
  int yyMajor,                  /* The major token to shift in */
  YYMINORTYPE *yypMinor         /* Pointer to the minor token to shift in */
){
  yyStackEntry *yytos;
  yypParser->yyidx++;
#ifdef YYTRACKMAXSTACKDEPTH
  if( yypParser->yyidx>yypParser->yyidxMax ){
    yypParser->yyidxMax = yypParser->yyidx;
  }
#endif
#if YYSTACKDEPTH>0 
  if( yypParser->yyidx>=YYSTACKDEPTH ){
    yyStackOverflow(yypParser, yypMinor);
    return;
  }
#else
  if( yypParser->yyidx>=yypParser->yystksz ){
    yyGrowStack(yypParser);
    if( yypParser->yyidx>=yypParser->yystksz ){
      yyStackOverflow(yypParser, yypMinor);
      return;
    }
  }
#endif
  yytos = &yypParser->yystack[yypParser->yyidx];
  yytos->stateno = (YYACTIONTYPE)yyNewState;
  yytos->major = (YYCODETYPE)yyMajor;
  yytos->minor = *yypMinor;
#ifndef NDEBUG
  if( yyTraceFILE && yypParser->yyidx>0 ){
    int i;
    fprintf(yyTraceFILE,"%sShift %d\n",yyTracePrompt,yyNewState);
    fprintf(yyTraceFILE,"%sStack:",yyTracePrompt);
    for(i=1; i<=yypParser->yyidx; i++)
      fprintf(yyTraceFILE," %s",yyTokenName[yypParser->yystack[i].major]);
    fprintf(yyTraceFILE,"\n");
  }
#endif
}

/* The following table contains information about every rule that
** is used during the reduce.
*/
static const struct {
  YYCODETYPE lhs;         /* Symbol on the left-hand side of the rule */
  unsigned char nrhs;     /* Number of right-hand side symbols in the rule */
} yyRuleInfo[] = {
  { 160, 2 },
  { 160, 2 },
  { 161, 3 },
  { 163, 3 },
  { 162, 1 },
  { 162, 1 },
  { 161, 1 },
  { 161, 3 },
  { 161, 4 },
  { 161, 3 },
  { 161, 2 },
  { 161, 3 },
  { 161, 3 },
  { 161, 5 },
  { 161, 5 },
  { 161, 4 },
  { 161, 4 },
  { 161, 3 },
  { 161, 2 },
  { 161, 3 },
  { 161, 3 },
  { 161, 5 },
  { 161, 3 },
  { 161, 4 },
  { 161, 2 },
  { 161, 3 },
  { 169, 2 },
  { 169, 2 },
  { 169, 2 },
  { 169, 2 },
  { 169, 3 },
  { 169, 3 },
  { 169, 3 },
  { 169, 3 },
  { 168, 0 },
  { 168, 1 },
  { 167, 0 },
  { 167, 1 },
  { 166, 3 },
  { 166, 4 },
  { 166, 4 },
  { 166, 4 },
  { 164, 0 },
  { 164, 1 },
  { 165, 1 },
  { 165, 1 },
  { 165, 1 },
  { 165, 2 },
  { 165, 2 },
  { 161, 2 },
  { 161, 3 },
  { 161, 2 },
  { 161, 4 },
  { 161, 5 },
  { 161, 9 },
  { 161, 10 },
  { 161, 6 },
  { 171, 0 },
  { 171, 2 },
  { 175, 5 },
  { 175, 4 },
  { 173, 0 },
  { 173, 3 },
  { 176, 3 },
  { 176, 1 },
  { 161, 4 },
  { 177, 0 },
  { 177, 2 },
  { 161, 6 },
  { 179, 5 },
  { 179, 3 },
  { 163, 1 },
  { 163, 4 },
  { 163, 4 },
  { 163, 4 },
  { 181, 0 },
  { 181, 1 },
  { 180, 9 },
  { 188, 0 },
  { 188, 2 },
  { 188, 4 },
  { 188, 4 },
  { 187, 0 },
  { 187, 3 },
  { 189, 4 },
  { 189, 2 },
  { 190, 1 },
  { 190, 1 },
  { 190, 0 },
  { 186, 0 },
  { 186, 2 },
  { 185, 0 },
  { 185, 3 },
  { 184, 0 },
  { 184, 2 },
  { 192, 4 },
  { 196, 4 },
  { 196, 0 },
  { 193, 0 },
  { 193, 2 },
  { 195, 2 },
  { 195, 0 },
  { 197, 1 },
  { 197, 1 },
  { 197, 2 },
  { 197, 2 },
  { 197, 2 },
  { 197, 2 },
  { 197, 3 },
  { 197, 2 },
  { 197, 3 },
  { 197, 2 },
  { 197, 3 },
  { 194, 2 },
  { 194, 2 },
  { 194, 4 },
  { 199, 2 },
  { 199, 0 },
  { 183, 3 },
  { 183, 2 },
  { 200, 1 },
  { 200, 3 },
  { 200, 5 },
  { 198, 2 },
  { 198, 2 },
  { 198, 0 },
  { 182, 0 },
  { 182, 1 },
  { 182, 1 },
  { 182, 3 },
  { 191, 3 },
  { 191, 1 },
  { 174, 0 },
  { 174, 3 },
  { 174, 1 },
  { 178, 1 },
  { 178, 1 },
  { 178, 3 },
  { 178, 1 },
  { 178, 4 },
  { 178, 4 },
  { 178, 4 },
  { 178, 6 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 2 },
  { 178, 2 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 3 },
  { 178, 5 },
  { 178, 6 },
  { 178, 2 },
  { 178, 2 },
  { 178, 2 },
  { 178, 3 },
  { 178, 3 },
  { 178, 5 },
  { 178, 5 },
  { 178, 6 },
  { 178, 6 },
  { 178, 5 },
  { 204, 1 },
  { 204, 0 },
  { 205, 5 },
  { 205, 4 },
  { 206, 2 },
  { 206, 0 },
  { 203, 1 },
  { 203, 2 },
  { 201, 1 },
  { 201, 1 },
  { 201, 1 },
  { 201, 1 },
  { 202, 4 },
  { 202, 4 },
  { 202, 1 },
  { 207, 0 },
  { 207, 2 },
  { 207, 2 },
  { 207, 2 },
  { 207, 2 },
  { 170, 1 },
  { 170, 1 },
  { 170, 1 },
  { 172, 1 },
  { 172, 3 },
  { 172, 5 },
};

static void yy_accept(yyParser*);  /* Forward Declaration */

/*
** Perform a reduce action and the shift that must immediately
** follow the reduce.
*/
static void yy_reduce(
  yyParser *yypParser,         /* The parser */
  int yyruleno                 /* Number of the rule by which to reduce */
){
  int yygoto;                     /* The next state */
  int yyact;                      /* The next action */
  YYMINORTYPE yygotominor;        /* The LHS of the rule reduced */
  yyStackEntry *yymsp;            /* The top of the parser's stack */
  int yysize;                     /* Amount to pop the stack */
  gda_lemon_oracle_parserARG_FETCH;
  yymsp = &yypParser->yystack[yypParser->yyidx];
#ifndef NDEBUG
  if( yyTraceFILE && yyruleno>=0 
        && yyruleno<(int)(sizeof(yyRuleName)/sizeof(yyRuleName[0])) ){
    fprintf(yyTraceFILE, "%sReduce [%s].\n", yyTracePrompt,
      yyRuleName[yyruleno]);
  }
#endif /* NDEBUG */

  /* Silence complaints from purify about yygotominor being uninitialized
  ** in some cases when it is copied into the stack after the following
  ** switch.  yygotominor is uninitialized when a rule reduces that does
  ** not set the value of its left-hand side nonterminal.  Leaving the
  ** value of the nonterminal uninitialized is utterly harmless as long
  ** as the value is never used.  So really the only thing this code
  ** accomplishes is to quieten purify.  
  **
  ** 2007-01-16:  The wireshark project (www.wireshark.org) reports that
  ** without this code, their parser segfaults.  I'm not sure what there
  ** parser is doing to make this happen.  This is the second bug report
  ** from wireshark this week.  Clearly they are stressing Lemon in ways
  ** that it has not been previously stressed...  (SQLite ticket #2172)
  */
  /*memset(&yygotominor, 0, sizeof(yygotominor));*/
  yygotominor = yyzerominor;


  switch( yyruleno ){
  /* Beginning here are the reduction cases.  A typical example
  ** follows:
  **   case 0:
  **  #line <lineno> <grammarfile>
  **     { ... }           // User supplied code
  **  #line <lineno> <thisfile>
  **     break;
  */
      case 0: /* stmt ::= cmd eos */
#line 279 "./parser.y"
{pdata->parsed_statement = yymsp[-1].minor.yy252;}
#line 2038 "parser.c"
        break;
      case 1: /* stmt ::= compound eos */
#line 280 "./parser.y"
{
	GdaSqlStatementCompound *scompound = (GdaSqlStatementCompound *) yymsp[-1].minor.yy252->contents;
	if (scompound->stmt_list->next)
		/* real compound (multiple statements) */
		pdata->parsed_statement = yymsp[-1].minor.yy252;
	else {
		/* false compound (only 1 select) */
		pdata->parsed_statement = (GdaSqlStatement*) scompound->stmt_list->data;
		GDA_SQL_ANY_PART (pdata->parsed_statement->contents)->parent = NULL;
		g_slist_free (scompound->stmt_list);
		scompound->stmt_list = NULL;
		gda_sql_statement_free (yymsp[-1].minor.yy252);
	}
}
#line 2056 "parser.c"
        break;
      case 2: /* cmd ::= LP cmd RP */
      case 3: /* compound ::= LP compound RP */ yytestcase(yyruleno==3);
#line 294 "./parser.y"
{yygotominor.yy252 = yymsp[-1].minor.yy252;  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 2064 "parser.c"
        break;
      case 4: /* eos ::= SEMI */
#line 297 "./parser.y"
{
  yy_destructor(yypParser,116,&yymsp[0].minor);
}
#line 2071 "parser.c"
        break;
      case 5: /* eos ::= END_OF_FILE */
#line 298 "./parser.y"
{
  yy_destructor(yypParser,117,&yymsp[0].minor);
}
#line 2078 "parser.c"
        break;
      case 6: /* cmd ::= BEGIN */
#line 306 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);  yy_destructor(yypParser,8,&yymsp[0].minor);
}
#line 2084 "parser.c"
        break;
      case 7: /* cmd ::= BEGIN TRANSACTION nm_opt */
#line 307 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					 gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,118,&yymsp[-1].minor);
}
#line 2093 "parser.c"
        break;
      case 8: /* cmd ::= BEGIN transtype TRANSACTION nm_opt */
#line 311 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						      gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[-2].minor.yy0);
						      gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
  yy_destructor(yypParser,118,&yymsp[-1].minor);
}
#line 2103 "parser.c"
        break;
      case 9: /* cmd ::= BEGIN transtype nm_opt */
#line 316 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					  gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[-1].minor.yy0);
					  gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
}
#line 2112 "parser.c"
        break;
      case 10: /* cmd ::= BEGIN transilev */
#line 321 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
				gda_sql_statement_trans_set_isol_level (yygotominor.yy252, yymsp[0].minor.yy197);
  yy_destructor(yypParser,8,&yymsp[-1].minor);
}
#line 2120 "parser.c"
        break;
      case 11: /* cmd ::= BEGIN TRANSACTION transilev */
#line 325 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					    gda_sql_statement_trans_set_isol_level (yygotominor.yy252, yymsp[0].minor.yy197);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,118,&yymsp[-1].minor);
}
#line 2129 "parser.c"
        break;
      case 12: /* cmd ::= BEGIN TRANSACTION transtype */
#line 329 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					    gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,118,&yymsp[-1].minor);
}
#line 2138 "parser.c"
        break;
      case 13: /* cmd ::= BEGIN TRANSACTION transtype opt_comma transilev */
#line 333 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
								   gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[-2].minor.yy0);
								   gda_sql_statement_trans_set_isol_level (yygotominor.yy252, yymsp[0].minor.yy197);
  yy_destructor(yypParser,8,&yymsp[-4].minor);
  yy_destructor(yypParser,118,&yymsp[-3].minor);
}
#line 2148 "parser.c"
        break;
      case 14: /* cmd ::= BEGIN TRANSACTION transilev opt_comma transtype */
#line 338 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
								   gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[0].minor.yy0);
								   gda_sql_statement_trans_set_isol_level (yygotominor.yy252, yymsp[-2].minor.yy197);
  yy_destructor(yypParser,8,&yymsp[-4].minor);
  yy_destructor(yypParser,118,&yymsp[-3].minor);
}
#line 2158 "parser.c"
        break;
      case 15: /* cmd ::= BEGIN transtype opt_comma transilev */
#line 343 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						       gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[-2].minor.yy0);
						       gda_sql_statement_trans_set_isol_level (yygotominor.yy252, yymsp[0].minor.yy197);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
}
#line 2167 "parser.c"
        break;
      case 16: /* cmd ::= BEGIN transilev opt_comma transtype */
#line 348 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						       gda_sql_statement_trans_take_mode (yygotominor.yy252, yymsp[0].minor.yy0);
						       gda_sql_statement_trans_set_isol_level (yygotominor.yy252, yymsp[-2].minor.yy197);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
}
#line 2176 "parser.c"
        break;
      case 17: /* cmd ::= END trans_opt_kw nm_opt */
#line 353 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
					gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,17,&yymsp[-2].minor);
}
#line 2184 "parser.c"
        break;
      case 18: /* cmd ::= COMMIT nm_opt */
#line 357 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
			      gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,48,&yymsp[-1].minor);
}
#line 2192 "parser.c"
        break;
      case 19: /* cmd ::= COMMIT TRANSACTION nm_opt */
#line 361 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
					  gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,48,&yymsp[-2].minor);
  yy_destructor(yypParser,118,&yymsp[-1].minor);
}
#line 2201 "parser.c"
        break;
      case 20: /* cmd ::= COMMIT FORCE STRING */
#line 365 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,48,&yymsp[-2].minor);
  yy_destructor(yypParser,62,&yymsp[-1].minor);
  yy_destructor(yypParser,119,&yymsp[0].minor);
}
#line 2209 "parser.c"
        break;
      case 21: /* cmd ::= COMMIT FORCE STRING COMMA INTEGER */
#line 366 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,48,&yymsp[-4].minor);
  yy_destructor(yypParser,62,&yymsp[-3].minor);
  yy_destructor(yypParser,119,&yymsp[-2].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
  yy_destructor(yypParser,121,&yymsp[0].minor);
}
#line 2219 "parser.c"
        break;
      case 22: /* cmd ::= COMMIT COMMENT STRING */
#line 367 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,48,&yymsp[-2].minor);
  yy_destructor(yypParser,61,&yymsp[-1].minor);
  yy_destructor(yypParser,119,&yymsp[0].minor);
}
#line 2227 "parser.c"
        break;
      case 23: /* cmd ::= COMMIT COMMENT STRING ora_commit_write */
#line 368 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,48,&yymsp[-3].minor);
  yy_destructor(yypParser,61,&yymsp[-2].minor);
  yy_destructor(yypParser,119,&yymsp[-1].minor);
}
#line 2235 "parser.c"
        break;
      case 24: /* cmd ::= COMMIT ora_commit_write */
#line 369 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,48,&yymsp[-1].minor);
}
#line 2241 "parser.c"
        break;
      case 25: /* cmd ::= ROLLBACK trans_opt_kw nm_opt */
#line 371 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK);
					     gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-2].minor);
}
#line 2249 "parser.c"
        break;
      case 26: /* ora_commit_write ::= WRITE IMMEDIATE */
#line 375 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-1].minor);
  yy_destructor(yypParser,23,&yymsp[0].minor);
}
#line 2257 "parser.c"
        break;
      case 27: /* ora_commit_write ::= WRITE BATCH */
#line 376 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2265 "parser.c"
        break;
      case 28: /* ora_commit_write ::= WRITE WAIT */
#line 377 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-1].minor);
  yy_destructor(yypParser,63,&yymsp[0].minor);
}
#line 2273 "parser.c"
        break;
      case 29: /* ora_commit_write ::= WRITE NOWAIT */
#line 378 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2281 "parser.c"
        break;
      case 30: /* ora_commit_write ::= WRITE IMMEDIATE WAIT */
#line 379 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-2].minor);
  yy_destructor(yypParser,23,&yymsp[-1].minor);
  yy_destructor(yypParser,63,&yymsp[0].minor);
}
#line 2290 "parser.c"
        break;
      case 31: /* ora_commit_write ::= WRITE IMMEDIATE NOWAIT */
#line 380 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-2].minor);
  yy_destructor(yypParser,23,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2299 "parser.c"
        break;
      case 32: /* ora_commit_write ::= WRITE BATCH WAIT */
#line 381 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-2].minor);
  yy_destructor(yypParser,65,&yymsp[-1].minor);
  yy_destructor(yypParser,63,&yymsp[0].minor);
}
#line 2308 "parser.c"
        break;
      case 33: /* ora_commit_write ::= WRITE BATCH NOWAIT */
#line 382 "./parser.y"
{
  yy_destructor(yypParser,57,&yymsp[-2].minor);
  yy_destructor(yypParser,65,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2317 "parser.c"
        break;
      case 35: /* trans_opt_kw ::= TRANSACTION */
#line 385 "./parser.y"
{
  yy_destructor(yypParser,118,&yymsp[0].minor);
}
#line 2324 "parser.c"
        break;
      case 37: /* opt_comma ::= COMMA */
#line 388 "./parser.y"
{
  yy_destructor(yypParser,120,&yymsp[0].minor);
}
#line 2331 "parser.c"
        break;
      case 38: /* transilev ::= ISOLATION LEVEL SERIALIZABLE */
#line 391 "./parser.y"
{yygotominor.yy197 = GDA_TRANSACTION_ISOLATION_SERIALIZABLE;  yy_destructor(yypParser,50,&yymsp[-2].minor);
  yy_destructor(yypParser,51,&yymsp[-1].minor);
  yy_destructor(yypParser,52,&yymsp[0].minor);
}
#line 2339 "parser.c"
        break;
      case 39: /* transilev ::= ISOLATION LEVEL REPEATABLE READ */
#line 392 "./parser.y"
{yygotominor.yy197 = GDA_TRANSACTION_ISOLATION_REPEATABLE_READ;  yy_destructor(yypParser,50,&yymsp[-3].minor);
  yy_destructor(yypParser,51,&yymsp[-2].minor);
  yy_destructor(yypParser,56,&yymsp[-1].minor);
  yy_destructor(yypParser,53,&yymsp[0].minor);
}
#line 2348 "parser.c"
        break;
      case 40: /* transilev ::= ISOLATION LEVEL READ COMMITTED */
#line 393 "./parser.y"
{yygotominor.yy197 = GDA_TRANSACTION_ISOLATION_READ_COMMITTED;  yy_destructor(yypParser,50,&yymsp[-3].minor);
  yy_destructor(yypParser,51,&yymsp[-2].minor);
  yy_destructor(yypParser,53,&yymsp[-1].minor);
  yy_destructor(yypParser,54,&yymsp[0].minor);
}
#line 2357 "parser.c"
        break;
      case 41: /* transilev ::= ISOLATION LEVEL READ UNCOMMITTED */
#line 394 "./parser.y"
{yygotominor.yy197 = GDA_TRANSACTION_ISOLATION_READ_UNCOMMITTED;  yy_destructor(yypParser,50,&yymsp[-3].minor);
  yy_destructor(yypParser,51,&yymsp[-2].minor);
  yy_destructor(yypParser,53,&yymsp[-1].minor);
  yy_destructor(yypParser,55,&yymsp[0].minor);
}
#line 2366 "parser.c"
        break;
      case 42: /* nm_opt ::= */
      case 57: /* opt_on_conflict ::= */ yytestcase(yyruleno==57);
      case 125: /* as ::= */ yytestcase(yyruleno==125);
#line 396 "./parser.y"
{yygotominor.yy0 = NULL;}
#line 2373 "parser.c"
        break;
      case 43: /* nm_opt ::= nm */
      case 44: /* transtype ::= DEFERRED */ yytestcase(yyruleno==44);
      case 45: /* transtype ::= IMMEDIATE */ yytestcase(yyruleno==45);
      case 46: /* transtype ::= EXCLUSIVE */ yytestcase(yyruleno==46);
      case 120: /* starname ::= STAR */ yytestcase(yyruleno==120);
      case 179: /* value ::= STRING */ yytestcase(yyruleno==179);
      case 180: /* value ::= INTEGER */ yytestcase(yyruleno==180);
      case 181: /* value ::= FLOAT */ yytestcase(yyruleno==181);
      case 190: /* nm ::= JOIN */ yytestcase(yyruleno==190);
      case 191: /* nm ::= ID */ yytestcase(yyruleno==191);
      case 192: /* nm ::= TEXTUAL */ yytestcase(yyruleno==192);
      case 193: /* fullname ::= nm */ yytestcase(yyruleno==193);
#line 397 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;}
#line 2389 "parser.c"
        break;
      case 47: /* transtype ::= READ WRITE */
#line 402 "./parser.y"
{yygotominor.yy0 = g_new0 (GValue, 1);
			      g_value_init (yygotominor.yy0, G_TYPE_STRING);
			      g_value_set_string (yygotominor.yy0, "READ_WRITE");
  yy_destructor(yypParser,53,&yymsp[-1].minor);
  yy_destructor(yypParser,57,&yymsp[0].minor);
}
#line 2399 "parser.c"
        break;
      case 48: /* transtype ::= READ ONLY */
#line 406 "./parser.y"
{yygotominor.yy0 = g_new0 (GValue, 1);
			     g_value_init (yygotominor.yy0, G_TYPE_STRING);
			     g_value_set_string (yygotominor.yy0, "READ_ONLY");
  yy_destructor(yypParser,53,&yymsp[-1].minor);
  yy_destructor(yypParser,58,&yymsp[0].minor);
}
#line 2409 "parser.c"
        break;
      case 49: /* cmd ::= SAVEPOINT nm */
#line 414 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_SAVEPOINT);
				    gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,59,&yymsp[-1].minor);
}
#line 2417 "parser.c"
        break;
      case 50: /* cmd ::= RELEASE SAVEPOINT nm */
#line 418 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE_SAVEPOINT);
				     gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,60,&yymsp[-2].minor);
  yy_destructor(yypParser,59,&yymsp[-1].minor);
}
#line 2426 "parser.c"
        break;
      case 51: /* cmd ::= RELEASE nm */
#line 422 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE_SAVEPOINT);
			   gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2434 "parser.c"
        break;
      case 52: /* cmd ::= ROLLBACK trans_opt_kw TO nm */
#line 426 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT);
					    gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-3].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2443 "parser.c"
        break;
      case 53: /* cmd ::= ROLLBACK trans_opt_kw TO SAVEPOINT nm */
#line 430 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT);
						      gda_sql_statement_trans_take_name (yygotominor.yy252, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-4].minor);
  yy_destructor(yypParser,122,&yymsp[-2].minor);
  yy_destructor(yypParser,59,&yymsp[-1].minor);
}
#line 2453 "parser.c"
        break;
      case 54: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP */
#line 437 "./parser.y"
{
	yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
	gda_sql_statement_insert_take_table_name (yygotominor.yy252, yymsp[-5].minor.yy0);
	gda_sql_statement_insert_take_fields_list (yygotominor.yy252, yymsp[-4].minor.yy105);
	gda_sql_statement_insert_take_1_values_list (yygotominor.yy252, g_slist_reverse (yymsp[-1].minor.yy33));
	gda_sql_statement_insert_take_on_conflict (yygotominor.yy252, yymsp[-7].minor.yy0);
  yy_destructor(yypParser,123,&yymsp[-8].minor);
  yy_destructor(yypParser,124,&yymsp[-6].minor);
  yy_destructor(yypParser,125,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 2469 "parser.c"
        break;
      case 55: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP ins_extra_values */
#line 445 "./parser.y"
{
	yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
	gda_sql_statement_insert_take_table_name (yygotominor.yy252, yymsp[-6].minor.yy0);
	gda_sql_statement_insert_take_fields_list (yygotominor.yy252, yymsp[-5].minor.yy105);
	gda_sql_statement_insert_take_1_values_list (yygotominor.yy252, g_slist_reverse (yymsp[-2].minor.yy33));
	gda_sql_statement_insert_take_extra_values_list (yygotominor.yy252, yymsp[0].minor.yy105);
	gda_sql_statement_insert_take_on_conflict (yygotominor.yy252, yymsp[-8].minor.yy0);
  yy_destructor(yypParser,123,&yymsp[-9].minor);
  yy_destructor(yypParser,124,&yymsp[-7].minor);
  yy_destructor(yypParser,125,&yymsp[-4].minor);
  yy_destructor(yypParser,101,&yymsp[-3].minor);
  yy_destructor(yypParser,102,&yymsp[-1].minor);
}
#line 2486 "parser.c"
        break;
      case 56: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt compound */
#line 454 "./parser.y"
{
        yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
        gda_sql_statement_insert_take_table_name (yygotominor.yy252, yymsp[-2].minor.yy0);
        gda_sql_statement_insert_take_fields_list (yygotominor.yy252, yymsp[-1].minor.yy105);
        gda_sql_statement_insert_take_select (yygotominor.yy252, yymsp[0].minor.yy252);
        gda_sql_statement_insert_take_on_conflict (yygotominor.yy252, yymsp[-4].minor.yy0);
  yy_destructor(yypParser,123,&yymsp[-5].minor);
  yy_destructor(yypParser,124,&yymsp[-3].minor);
}
#line 2499 "parser.c"
        break;
      case 58: /* opt_on_conflict ::= OR ID */
#line 464 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;  yy_destructor(yypParser,66,&yymsp[-1].minor);
}
#line 2505 "parser.c"
        break;
      case 59: /* ins_extra_values ::= ins_extra_values COMMA LP rexprlist RP */
#line 474 "./parser.y"
{yygotominor.yy105 = g_slist_append (yymsp[-4].minor.yy105, g_slist_reverse (yymsp[-1].minor.yy33));  yy_destructor(yypParser,120,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 2513 "parser.c"
        break;
      case 60: /* ins_extra_values ::= COMMA LP rexprlist RP */
#line 475 "./parser.y"
{yygotominor.yy105 = g_slist_append (NULL, g_slist_reverse (yymsp[-1].minor.yy33));  yy_destructor(yypParser,120,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 2521 "parser.c"
        break;
      case 61: /* inscollist_opt ::= */
      case 97: /* using_opt ::= */ yytestcase(yyruleno==97);
#line 479 "./parser.y"
{yygotominor.yy105 = NULL;}
#line 2527 "parser.c"
        break;
      case 62: /* inscollist_opt ::= LP inscollist RP */
#line 480 "./parser.y"
{yygotominor.yy105 = yymsp[-1].minor.yy105;  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 2534 "parser.c"
        break;
      case 63: /* inscollist ::= inscollist COMMA fullname */
#line 484 "./parser.y"
{GdaSqlField *field;
						    field = gda_sql_field_new (NULL);
						    gda_sql_field_take_name (field, yymsp[0].minor.yy0);
						    yygotominor.yy105 = g_slist_append (yymsp[-2].minor.yy105, field);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2544 "parser.c"
        break;
      case 64: /* inscollist ::= fullname */
#line 489 "./parser.y"
{GdaSqlField *field = gda_sql_field_new (NULL);
				gda_sql_field_take_name (field, yymsp[0].minor.yy0);
				yygotominor.yy105 = g_slist_prepend (NULL, field);
}
#line 2552 "parser.c"
        break;
      case 65: /* cmd ::= DELETE FROM fullname where_opt */
#line 495 "./parser.y"
{yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE); 
						  gda_sql_statement_delete_take_table_name (yygotominor.yy252, yymsp[-1].minor.yy0);
						  gda_sql_statement_delete_take_condition (yygotominor.yy252, yymsp[0].minor.yy302);  yy_destructor(yypParser,126,&yymsp[-3].minor);
  yy_destructor(yypParser,127,&yymsp[-2].minor);
}
#line 2561 "parser.c"
        break;
      case 66: /* where_opt ::= */
      case 89: /* having_opt ::= */ yytestcase(yyruleno==89);
      case 101: /* on_cond ::= */ yytestcase(yyruleno==101);
#line 501 "./parser.y"
{yygotominor.yy302 = NULL;}
#line 2568 "parser.c"
        break;
      case 67: /* where_opt ::= WHERE expr */
#line 502 "./parser.y"
{yygotominor.yy302 = yymsp[0].minor.yy302;  yy_destructor(yypParser,128,&yymsp[-1].minor);
}
#line 2574 "parser.c"
        break;
      case 68: /* cmd ::= UPDATE opt_on_conflict fullname SET setlist where_opt */
#line 505 "./parser.y"
{
	GSList *list;
	yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_UPDATE);
	gda_sql_statement_update_take_table_name (yygotominor.yy252, yymsp[-3].minor.yy0);
	gda_sql_statement_update_take_on_conflict (yygotominor.yy252, yymsp[-4].minor.yy0);
	gda_sql_statement_update_take_condition (yygotominor.yy252, yymsp[0].minor.yy302);
	for (list = yymsp[-1].minor.yy105; list; list = list->next) {
		UpdateSet *set = (UpdateSet*) list->data;
		gda_sql_statement_update_take_set_value (yygotominor.yy252, set->fname, set->expr);
		g_free (set);
	}
	g_slist_free (yymsp[-1].minor.yy105);
  yy_destructor(yypParser,129,&yymsp[-5].minor);
  yy_destructor(yypParser,130,&yymsp[-2].minor);
}
#line 2593 "parser.c"
        break;
      case 69: /* setlist ::= setlist COMMA fullname EQ expr */
#line 529 "./parser.y"
{UpdateSet *set;
							 set = g_new (UpdateSet, 1);
							 set->fname = yymsp[-2].minor.yy0;
							 set->expr = yymsp[0].minor.yy302;
							 yygotominor.yy105 = g_slist_append (yymsp[-4].minor.yy105, set);
  yy_destructor(yypParser,120,&yymsp[-3].minor);
  yy_destructor(yypParser,75,&yymsp[-1].minor);
}
#line 2605 "parser.c"
        break;
      case 70: /* setlist ::= fullname EQ expr */
#line 535 "./parser.y"
{UpdateSet *set;
					set = g_new (UpdateSet, 1);
					set->fname = yymsp[-2].minor.yy0;
					set->expr = yymsp[0].minor.yy302;
					yygotominor.yy105 = g_slist_append (NULL, set);
  yy_destructor(yypParser,75,&yymsp[-1].minor);
}
#line 2616 "parser.c"
        break;
      case 71: /* compound ::= selectcmd */
#line 546 "./parser.y"
{
	yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMPOUND);
	gda_sql_statement_compound_take_stmt (yygotominor.yy252, yymsp[0].minor.yy252);
}
#line 2624 "parser.c"
        break;
      case 72: /* compound ::= compound UNION opt_compound_all compound */
#line 550 "./parser.y"
{
	yygotominor.yy252 = compose_multiple_compounds (yymsp[-1].minor.yy100 ? GDA_SQL_STATEMENT_COMPOUND_UNION_ALL : GDA_SQL_STATEMENT_COMPOUND_UNION,
					yymsp[-3].minor.yy252, yymsp[0].minor.yy252);
  yy_destructor(yypParser,110,&yymsp[-2].minor);
}
#line 2633 "parser.c"
        break;
      case 73: /* compound ::= compound EXCEPT opt_compound_all compound */
#line 555 "./parser.y"
{
	yygotominor.yy252 = compose_multiple_compounds (yymsp[-1].minor.yy100 ? GDA_SQL_STATEMENT_COMPOUND_EXCEPT_ALL : GDA_SQL_STATEMENT_COMPOUND_EXCEPT,
					yymsp[-3].minor.yy252, yymsp[0].minor.yy252);
  yy_destructor(yypParser,111,&yymsp[-2].minor);
}
#line 2642 "parser.c"
        break;
      case 74: /* compound ::= compound INTERSECT opt_compound_all compound */
#line 560 "./parser.y"
{
	yygotominor.yy252 = compose_multiple_compounds (yymsp[-1].minor.yy100 ? GDA_SQL_STATEMENT_COMPOUND_INTERSECT_ALL : GDA_SQL_STATEMENT_COMPOUND_INTERSECT,
					yymsp[-3].minor.yy252, yymsp[0].minor.yy252);
  yy_destructor(yypParser,112,&yymsp[-2].minor);
}
#line 2651 "parser.c"
        break;
      case 75: /* opt_compound_all ::= */
#line 566 "./parser.y"
{yygotominor.yy100 = FALSE;}
#line 2656 "parser.c"
        break;
      case 76: /* opt_compound_all ::= ALL */
#line 567 "./parser.y"
{yygotominor.yy100 = TRUE;  yy_destructor(yypParser,131,&yymsp[0].minor);
}
#line 2662 "parser.c"
        break;
      case 77: /* selectcmd ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 574 "./parser.y"
{
	yygotominor.yy252 = gda_sql_statement_new (GDA_SQL_STATEMENT_SELECT);
	if (yymsp[-7].minor.yy249) {
		gda_sql_statement_select_take_distinct (yygotominor.yy252, yymsp[-7].minor.yy249->distinct, yymsp[-7].minor.yy249->expr);
		g_free (yymsp[-7].minor.yy249);
	}
	gda_sql_statement_select_take_expr_list (yygotominor.yy252, yymsp[-6].minor.yy33);
	gda_sql_statement_select_take_from (yygotominor.yy252, yymsp[-5].minor.yy259);
	gda_sql_statement_select_take_where_cond (yygotominor.yy252, yymsp[-4].minor.yy302);
	gda_sql_statement_select_take_group_by (yygotominor.yy252, yymsp[-3].minor.yy105);
	gda_sql_statement_select_take_having_cond (yygotominor.yy252, yymsp[-2].minor.yy302);
	gda_sql_statement_select_take_order_by (yygotominor.yy252, yymsp[-1].minor.yy33);
	gda_sql_statement_select_take_limits (yygotominor.yy252, yymsp[0].minor.yy408.count, yymsp[0].minor.yy408.offset);
  yy_destructor(yypParser,132,&yymsp[-8].minor);
}
#line 2681 "parser.c"
        break;
      case 78: /* limit_opt ::= */
#line 591 "./parser.y"
{yygotominor.yy408.count = NULL; yygotominor.yy408.offset = NULL;}
#line 2686 "parser.c"
        break;
      case 79: /* limit_opt ::= LIMIT expr */
#line 592 "./parser.y"
{yygotominor.yy408.count = yymsp[0].minor.yy302; yygotominor.yy408.offset = NULL;  yy_destructor(yypParser,133,&yymsp[-1].minor);
}
#line 2692 "parser.c"
        break;
      case 80: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 593 "./parser.y"
{yygotominor.yy408.count = yymsp[-2].minor.yy302; yygotominor.yy408.offset = yymsp[0].minor.yy302;  yy_destructor(yypParser,133,&yymsp[-3].minor);
  yy_destructor(yypParser,32,&yymsp[-1].minor);
}
#line 2699 "parser.c"
        break;
      case 81: /* limit_opt ::= LIMIT expr COMMA expr */
#line 594 "./parser.y"
{yygotominor.yy408.count = yymsp[-2].minor.yy302; yygotominor.yy408.offset = yymsp[0].minor.yy302;  yy_destructor(yypParser,133,&yymsp[-3].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2706 "parser.c"
        break;
      case 82: /* orderby_opt ::= */
#line 598 "./parser.y"
{yygotominor.yy33 = 0;}
#line 2711 "parser.c"
        break;
      case 83: /* orderby_opt ::= ORDER BY sortlist */
#line 599 "./parser.y"
{yygotominor.yy33 = yymsp[0].minor.yy33;  yy_destructor(yypParser,134,&yymsp[-2].minor);
  yy_destructor(yypParser,135,&yymsp[-1].minor);
}
#line 2718 "parser.c"
        break;
      case 84: /* sortlist ::= sortlist COMMA expr sortorder */
#line 603 "./parser.y"
{GdaSqlSelectOrder *order;
							 order = gda_sql_select_order_new (NULL);
							 order->expr = yymsp[-1].minor.yy302;
							 order->asc = yymsp[0].minor.yy100;
							 yygotominor.yy33 = g_slist_append (yymsp[-3].minor.yy33, order);
  yy_destructor(yypParser,120,&yymsp[-2].minor);
}
#line 2729 "parser.c"
        break;
      case 85: /* sortlist ::= expr sortorder */
#line 609 "./parser.y"
{GdaSqlSelectOrder *order;
				       order = gda_sql_select_order_new (NULL);
				       order->expr = yymsp[-1].minor.yy302;
				       order->asc = yymsp[0].minor.yy100;
				       yygotominor.yy33 = g_slist_prepend (NULL, order);
}
#line 2739 "parser.c"
        break;
      case 86: /* sortorder ::= ASC */
#line 617 "./parser.y"
{yygotominor.yy100 = TRUE;  yy_destructor(yypParser,5,&yymsp[0].minor);
}
#line 2745 "parser.c"
        break;
      case 87: /* sortorder ::= DESC */
#line 618 "./parser.y"
{yygotominor.yy100 = FALSE;  yy_destructor(yypParser,14,&yymsp[0].minor);
}
#line 2751 "parser.c"
        break;
      case 88: /* sortorder ::= */
#line 619 "./parser.y"
{yygotominor.yy100 = TRUE;}
#line 2756 "parser.c"
        break;
      case 90: /* having_opt ::= HAVING expr */
#line 625 "./parser.y"
{yygotominor.yy302 = yymsp[0].minor.yy302;  yy_destructor(yypParser,136,&yymsp[-1].minor);
}
#line 2762 "parser.c"
        break;
      case 91: /* groupby_opt ::= */
#line 629 "./parser.y"
{yygotominor.yy105 = 0;}
#line 2767 "parser.c"
        break;
      case 92: /* groupby_opt ::= GROUP BY rnexprlist */
#line 630 "./parser.y"
{yygotominor.yy105 = g_slist_reverse (yymsp[0].minor.yy33);  yy_destructor(yypParser,137,&yymsp[-2].minor);
  yy_destructor(yypParser,135,&yymsp[-1].minor);
}
#line 2774 "parser.c"
        break;
      case 93: /* from ::= */
      case 98: /* stl_prefix ::= */ yytestcase(yyruleno==98);
#line 634 "./parser.y"
{yygotominor.yy259 = NULL;}
#line 2780 "parser.c"
        break;
      case 94: /* from ::= FROM seltablist */
#line 635 "./parser.y"
{yygotominor.yy259 = yymsp[0].minor.yy259;  yy_destructor(yypParser,127,&yymsp[-1].minor);
}
#line 2786 "parser.c"
        break;
      case 95: /* seltablist ::= stl_prefix seltarget on_cond using_opt */
#line 642 "./parser.y"
{
	GSList *last;
	if (yymsp[-3].minor.yy259)
		yygotominor.yy259 = yymsp[-3].minor.yy259;
	else 
		yygotominor.yy259 = gda_sql_select_from_new (NULL);
	gda_sql_select_from_take_new_target (yygotominor.yy259, yymsp[-2].minor.yy414);
	last = g_slist_last (yygotominor.yy259->joins);
	if (last) {
		GdaSqlSelectJoin *join = (GdaSqlSelectJoin *) (last->data);
		join->expr = yymsp[-1].minor.yy302;
		join->position = g_slist_length (yygotominor.yy259->targets) - 1;
		join->use = yymsp[0].minor.yy105;
	}
}
#line 2805 "parser.c"
        break;
      case 96: /* using_opt ::= USING LP inscollist RP */
#line 660 "./parser.y"
{yygotominor.yy105 = yymsp[-1].minor.yy105;  yy_destructor(yypParser,138,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 2813 "parser.c"
        break;
      case 99: /* stl_prefix ::= seltablist jointype */
#line 664 "./parser.y"
{GdaSqlSelectJoin *join;
					      yygotominor.yy259 = yymsp[-1].minor.yy259;
					      join = gda_sql_select_join_new (GDA_SQL_ANY_PART (yygotominor.yy259));
					      join->type = yymsp[0].minor.yy367;
					      gda_sql_select_from_take_new_join (yygotominor.yy259, join);
}
#line 2823 "parser.c"
        break;
      case 100: /* on_cond ::= ON expr */
#line 674 "./parser.y"
{yygotominor.yy302 = yymsp[0].minor.yy302;  yy_destructor(yypParser,139,&yymsp[-1].minor);
}
#line 2829 "parser.c"
        break;
      case 102: /* jointype ::= COMMA */
#line 678 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_CROSS;  yy_destructor(yypParser,120,&yymsp[0].minor);
}
#line 2835 "parser.c"
        break;
      case 103: /* jointype ::= JOIN */
#line 679 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_INNER;  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2841 "parser.c"
        break;
      case 104: /* jointype ::= CROSS JOIN */
#line 680 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_CROSS;  yy_destructor(yypParser,109,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2848 "parser.c"
        break;
      case 105: /* jointype ::= INNER JOIN */
#line 681 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_INNER;  yy_destructor(yypParser,104,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2855 "parser.c"
        break;
      case 106: /* jointype ::= NATURAL JOIN */
#line 682 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_NATURAL;  yy_destructor(yypParser,105,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2862 "parser.c"
        break;
      case 107: /* jointype ::= LEFT JOIN */
#line 683 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_LEFT;  yy_destructor(yypParser,106,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2869 "parser.c"
        break;
      case 108: /* jointype ::= LEFT OUTER JOIN */
#line 684 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_LEFT;  yy_destructor(yypParser,106,&yymsp[-2].minor);
  yy_destructor(yypParser,140,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2877 "parser.c"
        break;
      case 109: /* jointype ::= RIGHT JOIN */
#line 685 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_RIGHT;  yy_destructor(yypParser,107,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2884 "parser.c"
        break;
      case 110: /* jointype ::= RIGHT OUTER JOIN */
#line 686 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_RIGHT;  yy_destructor(yypParser,107,&yymsp[-2].minor);
  yy_destructor(yypParser,140,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2892 "parser.c"
        break;
      case 111: /* jointype ::= FULL JOIN */
#line 687 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_FULL;  yy_destructor(yypParser,108,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2899 "parser.c"
        break;
      case 112: /* jointype ::= FULL OUTER JOIN */
#line 688 "./parser.y"
{yygotominor.yy367 = GDA_SQL_SELECT_JOIN_FULL;  yy_destructor(yypParser,108,&yymsp[-2].minor);
  yy_destructor(yypParser,140,&yymsp[-1].minor);
  yy_destructor(yypParser,103,&yymsp[0].minor);
}
#line 2907 "parser.c"
        break;
      case 113: /* seltarget ::= fullname as */
#line 693 "./parser.y"
{yygotominor.yy414 = gda_sql_select_target_new (NULL);
				     gda_sql_select_target_take_alias (yygotominor.yy414, yymsp[0].minor.yy0);
				     gda_sql_select_target_take_table_name (yygotominor.yy414, yymsp[-1].minor.yy0);
}
#line 2915 "parser.c"
        break;
      case 114: /* seltarget ::= fullname ID */
#line 697 "./parser.y"
{yygotominor.yy414 = gda_sql_select_target_new (NULL);
                                     gda_sql_select_target_take_alias (yygotominor.yy414, yymsp[0].minor.yy0);
                                     gda_sql_select_target_take_table_name (yygotominor.yy414, yymsp[-1].minor.yy0);
}
#line 2923 "parser.c"
        break;
      case 115: /* seltarget ::= LP compound RP as */
#line 701 "./parser.y"
{yygotominor.yy414 = gda_sql_select_target_new (NULL);
					     gda_sql_select_target_take_alias (yygotominor.yy414, yymsp[0].minor.yy0);
					     gda_sql_select_target_take_select (yygotominor.yy414, yymsp[-2].minor.yy252);
  yy_destructor(yypParser,101,&yymsp[-3].minor);
  yy_destructor(yypParser,102,&yymsp[-1].minor);
}
#line 2933 "parser.c"
        break;
      case 116: /* sclp ::= selcollist COMMA */
#line 711 "./parser.y"
{yygotominor.yy33 = yymsp[-1].minor.yy33;  yy_destructor(yypParser,120,&yymsp[0].minor);
}
#line 2939 "parser.c"
        break;
      case 117: /* sclp ::= */
      case 132: /* rexprlist ::= */ yytestcase(yyruleno==132);
#line 712 "./parser.y"
{yygotominor.yy33 = NULL;}
#line 2945 "parser.c"
        break;
      case 118: /* selcollist ::= sclp expr as */
#line 714 "./parser.y"
{GdaSqlSelectField *field;
					  field = gda_sql_select_field_new (NULL);
					  gda_sql_select_field_take_expr (field, yymsp[-1].minor.yy302);
					  gda_sql_select_field_take_alias (field, yymsp[0].minor.yy0); 
					  yygotominor.yy33 = g_slist_append (yymsp[-2].minor.yy33, field);}
#line 2954 "parser.c"
        break;
      case 119: /* selcollist ::= sclp starname */
#line 719 "./parser.y"
{GdaSqlSelectField *field;
					field = gda_sql_select_field_new (NULL);
					gda_sql_select_field_take_star_value (field, yymsp[0].minor.yy0);
					yygotominor.yy33 = g_slist_append (yymsp[-1].minor.yy33, field);}
#line 2962 "parser.c"
        break;
      case 121: /* starname ::= nm DOT STAR */
      case 194: /* fullname ::= nm DOT nm */ yytestcase(yyruleno==194);
#line 725 "./parser.y"
{gchar *str;
				  str = g_strdup_printf ("%s.%s", g_value_get_string (yymsp[-2].minor.yy0), g_value_get_string (yymsp[0].minor.yy0));
				  yygotominor.yy0 = g_new0 (GValue, 1);
				  g_value_init (yygotominor.yy0, G_TYPE_STRING);
				  g_value_take_string (yygotominor.yy0, str);
				  g_value_reset (yymsp[-2].minor.yy0); g_free (yymsp[-2].minor.yy0);
				  g_value_reset (yymsp[0].minor.yy0); g_free (yymsp[0].minor.yy0);
  yy_destructor(yypParser,141,&yymsp[-1].minor);
}
#line 2976 "parser.c"
        break;
      case 122: /* starname ::= nm DOT nm DOT STAR */
      case 195: /* fullname ::= nm DOT nm DOT nm */ yytestcase(yyruleno==195);
#line 734 "./parser.y"
{gchar *str;
				  str = g_strdup_printf ("%s.%s.%s", g_value_get_string (yymsp[-4].minor.yy0), 
							 g_value_get_string (yymsp[-2].minor.yy0), g_value_get_string (yymsp[0].minor.yy0));
				  yygotominor.yy0 = g_new0 (GValue, 1);
				  g_value_init (yygotominor.yy0, G_TYPE_STRING);
				  g_value_take_string (yygotominor.yy0, str);
				  g_value_reset (yymsp[-4].minor.yy0); g_free (yymsp[-4].minor.yy0);
				  g_value_reset (yymsp[-2].minor.yy0); g_free (yymsp[-2].minor.yy0);
				  g_value_reset (yymsp[0].minor.yy0); g_free (yymsp[0].minor.yy0);
  yy_destructor(yypParser,141,&yymsp[-3].minor);
  yy_destructor(yypParser,141,&yymsp[-1].minor);
}
#line 2993 "parser.c"
        break;
      case 123: /* as ::= AS fullname */
      case 124: /* as ::= AS value */ yytestcase(yyruleno==124);
#line 745 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;  yy_destructor(yypParser,142,&yymsp[-1].minor);
}
#line 3000 "parser.c"
        break;
      case 126: /* distinct ::= */
#line 751 "./parser.y"
{yygotominor.yy249 = NULL;}
#line 3005 "parser.c"
        break;
      case 127: /* distinct ::= ALL */
#line 752 "./parser.y"
{yygotominor.yy249 = NULL;  yy_destructor(yypParser,131,&yymsp[0].minor);
}
#line 3011 "parser.c"
        break;
      case 128: /* distinct ::= DISTINCT */
#line 753 "./parser.y"
{yygotominor.yy249 = g_new0 (Distinct, 1); yygotominor.yy249->distinct = TRUE;  yy_destructor(yypParser,143,&yymsp[0].minor);
}
#line 3017 "parser.c"
        break;
      case 129: /* distinct ::= DISTINCT ON expr */
#line 754 "./parser.y"
{yygotominor.yy249 = g_new0 (Distinct, 1); yygotominor.yy249->distinct = TRUE; yygotominor.yy249->expr = yymsp[0].minor.yy302;  yy_destructor(yypParser,143,&yymsp[-2].minor);
  yy_destructor(yypParser,139,&yymsp[-1].minor);
}
#line 3024 "parser.c"
        break;
      case 130: /* rnexprlist ::= rnexprlist COMMA expr */
      case 133: /* rexprlist ::= rexprlist COMMA expr */ yytestcase(yyruleno==133);
#line 759 "./parser.y"
{yygotominor.yy33 = g_slist_prepend (yymsp[-2].minor.yy33, yymsp[0].minor.yy302);  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 3031 "parser.c"
        break;
      case 131: /* rnexprlist ::= expr */
      case 134: /* rexprlist ::= expr */ yytestcase(yyruleno==134);
#line 760 "./parser.y"
{yygotominor.yy33 = g_slist_append (NULL, yymsp[0].minor.yy302);}
#line 3037 "parser.c"
        break;
      case 135: /* expr ::= pvalue */
#line 772 "./parser.y"
{yygotominor.yy302 = yymsp[0].minor.yy302;}
#line 3042 "parser.c"
        break;
      case 136: /* expr ::= value */
      case 138: /* expr ::= fullname */ yytestcase(yyruleno==138);
#line 773 "./parser.y"
{yygotominor.yy302 = gda_sql_expr_new (NULL); yygotominor.yy302->value = yymsp[0].minor.yy0;}
#line 3048 "parser.c"
        break;
      case 137: /* expr ::= LP expr RP */
#line 774 "./parser.y"
{yygotominor.yy302 = yymsp[-1].minor.yy302;  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3055 "parser.c"
        break;
      case 139: /* expr ::= fullname LP rexprlist RP */
#line 776 "./parser.y"
{GdaSqlFunction *func;
					    yygotominor.yy302 = gda_sql_expr_new (NULL); 
					    func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy302)); 
					    gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					    gda_sql_function_take_args_list (func, g_slist_reverse (yymsp[-1].minor.yy33));
					    yygotominor.yy302->func = func;  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3067 "parser.c"
        break;
      case 140: /* expr ::= fullname LP compound RP */
#line 782 "./parser.y"
{GdaSqlFunction *func;
					     GdaSqlExpr *expr;
					     yygotominor.yy302 = gda_sql_expr_new (NULL); 
					     func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy302)); 
					     gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					     expr = gda_sql_expr_new (GDA_SQL_ANY_PART (func)); 
					     gda_sql_expr_take_select (expr, yymsp[-1].minor.yy252);
					     gda_sql_function_take_args_list (func, g_slist_prepend (NULL, expr));
					     yygotominor.yy302->func = func;  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3082 "parser.c"
        break;
      case 141: /* expr ::= fullname LP starname RP */
#line 791 "./parser.y"
{GdaSqlFunction *func;
					    GdaSqlExpr *expr;
					    yygotominor.yy302 = gda_sql_expr_new (NULL); 
					    func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy302));
					    gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					    expr = gda_sql_expr_new (GDA_SQL_ANY_PART (func)); 
					    expr->value = yymsp[-1].minor.yy0;
					    gda_sql_function_take_args_list (func, g_slist_prepend (NULL, expr));
					    yygotominor.yy302->func = func;  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3097 "parser.c"
        break;
      case 142: /* expr ::= CAST LP expr AS fullname RP */
#line 800 "./parser.y"
{yygotominor.yy302 = yymsp[-3].minor.yy302;
						yymsp[-3].minor.yy302->cast_as = g_value_dup_string (yymsp[-1].minor.yy0);
						g_value_reset (yymsp[-1].minor.yy0);
						g_free (yymsp[-1].minor.yy0);  yy_destructor(yypParser,10,&yymsp[-5].minor);
  yy_destructor(yypParser,101,&yymsp[-4].minor);
  yy_destructor(yypParser,142,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3109 "parser.c"
        break;
      case 143: /* expr ::= expr PGCAST fullname */
#line 804 "./parser.y"
{yygotominor.yy302 = yymsp[-2].minor.yy302;
					 yymsp[-2].minor.yy302->cast_as = g_value_dup_string (yymsp[0].minor.yy0);
					 g_value_reset (yymsp[0].minor.yy0);
					 g_free (yymsp[0].minor.yy0);  yy_destructor(yypParser,113,&yymsp[-1].minor);
}
#line 3118 "parser.c"
        break;
      case 144: /* expr ::= expr PLUS|MINUS expr */
#line 809 "./parser.y"
{yygotominor.yy302 = compose_multiple_expr (string_to_op_type (yymsp[-1].minor.yy0), yymsp[-2].minor.yy302, yymsp[0].minor.yy302);}
#line 3123 "parser.c"
        break;
      case 145: /* expr ::= expr STAR expr */
#line 810 "./parser.y"
{yygotominor.yy302 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_STAR, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,93,&yymsp[-1].minor);
}
#line 3129 "parser.c"
        break;
      case 146: /* expr ::= expr SLASH|REM expr */
      case 147: /* expr ::= expr BITAND|BITOR expr */ yytestcase(yyruleno==147);
      case 153: /* expr ::= expr GT|LEQ|GEQ|LT expr */ yytestcase(yyruleno==153);
      case 154: /* expr ::= expr DIFF|EQ expr */ yytestcase(yyruleno==154);
      case 157: /* expr ::= expr REGEXP|REGEXP_CI|NOT_REGEXP|NOT_REGEXP_CI|SIMILAR expr */ yytestcase(yyruleno==157);
#line 811 "./parser.y"
{yygotominor.yy302 = create_two_expr (string_to_op_type (yymsp[-1].minor.yy0), yymsp[-2].minor.yy302, yymsp[0].minor.yy302);}
#line 3138 "parser.c"
        break;
      case 148: /* expr ::= MINUS expr */
#line 814 "./parser.y"
{yygotominor.yy302 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_MINUS, yymsp[0].minor.yy302);  yy_destructor(yypParser,92,&yymsp[-1].minor);
}
#line 3144 "parser.c"
        break;
      case 149: /* expr ::= PLUS expr */
#line 815 "./parser.y"
{yygotominor.yy302 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_PLUS, yymsp[0].minor.yy302);  yy_destructor(yypParser,91,&yymsp[-1].minor);
}
#line 3150 "parser.c"
        break;
      case 150: /* expr ::= expr AND expr */
#line 817 "./parser.y"
{yygotominor.yy302 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_AND, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,67,&yymsp[-1].minor);
}
#line 3156 "parser.c"
        break;
      case 151: /* expr ::= expr OR expr */
#line 818 "./parser.y"
{yygotominor.yy302 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_OR, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,66,&yymsp[-1].minor);
}
#line 3162 "parser.c"
        break;
      case 152: /* expr ::= expr CONCAT expr */
#line 819 "./parser.y"
{yygotominor.yy302 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_CONCAT, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,96,&yymsp[-1].minor);
}
#line 3168 "parser.c"
        break;
      case 155: /* expr ::= expr LIKE expr */
#line 823 "./parser.y"
{yygotominor.yy302 = create_two_expr (GDA_SQL_OPERATOR_TYPE_LIKE, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,26,&yymsp[-1].minor);
}
#line 3174 "parser.c"
        break;
      case 156: /* expr ::= expr NOTLIKE expr */
#line 824 "./parser.y"
{yygotominor.yy302 = create_two_expr (GDA_SQL_OPERATOR_TYPE_NOTLIKE, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,70,&yymsp[-1].minor);
}
#line 3180 "parser.c"
        break;
      case 158: /* expr ::= expr BETWEEN expr AND expr */
#line 826 "./parser.y"
{GdaSqlOperation *cond;
						  yygotominor.yy302 = gda_sql_expr_new (NULL);
						  cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy302));
						  yygotominor.yy302->cond = cond;
						  cond->operator_type = GDA_SQL_OPERATOR_TYPE_BETWEEN;
						  cond->operands = g_slist_append (NULL, yymsp[-4].minor.yy302);
						  GDA_SQL_ANY_PART (yymsp[-4].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
						  cond->operands = g_slist_append (cond->operands, yymsp[-2].minor.yy302);
						  GDA_SQL_ANY_PART (yymsp[-2].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
						  cond->operands = g_slist_append (cond->operands, yymsp[0].minor.yy302);
						  GDA_SQL_ANY_PART (yymsp[0].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,76,&yymsp[-3].minor);
  yy_destructor(yypParser,67,&yymsp[-1].minor);
}
#line 3198 "parser.c"
        break;
      case 159: /* expr ::= expr NOT BETWEEN expr AND expr */
#line 839 "./parser.y"
{GdaSqlOperation *cond;
						      GdaSqlExpr *expr;
						      expr = gda_sql_expr_new (NULL);
						      cond = gda_sql_operation_new (GDA_SQL_ANY_PART (expr));
						      expr->cond = cond;
						      cond->operator_type = GDA_SQL_OPERATOR_TYPE_BETWEEN;
						      cond->operands = g_slist_append (NULL, yymsp[-5].minor.yy302);
						      GDA_SQL_ANY_PART (yymsp[-5].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
						      cond->operands = g_slist_append (cond->operands, yymsp[-2].minor.yy302);
						      GDA_SQL_ANY_PART (yymsp[-2].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
						      cond->operands = g_slist_append (cond->operands, yymsp[0].minor.yy302);
						      GDA_SQL_ANY_PART (yymsp[0].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);

						      yygotominor.yy302 = gda_sql_expr_new (NULL);
						      cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy302));
						      yygotominor.yy302->cond = cond;
						      cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOT;
						      cond->operands = g_slist_prepend (NULL, expr);
						      GDA_SQL_ANY_PART (expr)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,68,&yymsp[-4].minor);
  yy_destructor(yypParser,76,&yymsp[-3].minor);
  yy_destructor(yypParser,67,&yymsp[-1].minor);
}
#line 3225 "parser.c"
        break;
      case 160: /* expr ::= NOT expr */
#line 860 "./parser.y"
{yygotominor.yy302 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_NOT, yymsp[0].minor.yy302);  yy_destructor(yypParser,68,&yymsp[-1].minor);
}
#line 3231 "parser.c"
        break;
      case 161: /* expr ::= BITNOT expr */
#line 861 "./parser.y"
{yygotominor.yy302 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_BITNOT, yymsp[0].minor.yy302);  yy_destructor(yypParser,100,&yymsp[-1].minor);
}
#line 3237 "parser.c"
        break;
      case 162: /* expr ::= expr uni_op */
#line 862 "./parser.y"
{yygotominor.yy302 = create_uni_expr (yymsp[0].minor.yy295, yymsp[-1].minor.yy302);}
#line 3242 "parser.c"
        break;
      case 163: /* expr ::= expr IS expr */
#line 864 "./parser.y"
{yygotominor.yy302 = create_two_expr (GDA_SQL_OPERATOR_TYPE_IS, yymsp[-2].minor.yy302, yymsp[0].minor.yy302);  yy_destructor(yypParser,69,&yymsp[-1].minor);
}
#line 3248 "parser.c"
        break;
      case 164: /* expr ::= LP compound RP */
#line 865 "./parser.y"
{yygotominor.yy302 = gda_sql_expr_new (NULL); gda_sql_expr_take_select (yygotominor.yy302, yymsp[-1].minor.yy252);  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3255 "parser.c"
        break;
      case 165: /* expr ::= expr IN LP rexprlist RP */
#line 866 "./parser.y"
{GdaSqlOperation *cond;
					   GSList *list;
					   yygotominor.yy302 = gda_sql_expr_new (NULL);
					   cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy302));
					   yygotominor.yy302->cond = cond;
					   cond->operator_type = GDA_SQL_OPERATOR_TYPE_IN;
					   cond->operands = g_slist_prepend (g_slist_reverse (yymsp[-1].minor.yy33), yymsp[-4].minor.yy302);
					   for (list = cond->operands; list; list = list->next)
						   GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,71,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3272 "parser.c"
        break;
      case 166: /* expr ::= expr IN LP compound RP */
#line 876 "./parser.y"
{GdaSqlOperation *cond;
					    GdaSqlExpr *expr;
					    yygotominor.yy302 = gda_sql_expr_new (NULL);
					    cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy302));
					    yygotominor.yy302->cond = cond;
					    cond->operator_type = GDA_SQL_OPERATOR_TYPE_IN;
					    
					    expr = gda_sql_expr_new (GDA_SQL_ANY_PART (cond));
					    gda_sql_expr_take_select (expr, yymsp[-1].minor.yy252);
					    cond->operands = g_slist_prepend (NULL, expr);
					    cond->operands = g_slist_prepend (cond->operands, yymsp[-4].minor.yy302);
					    GDA_SQL_ANY_PART (yymsp[-4].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,71,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3292 "parser.c"
        break;
      case 167: /* expr ::= expr NOT IN LP rexprlist RP */
#line 889 "./parser.y"
{GdaSqlOperation *cond;
					       GSList *list;
					       yygotominor.yy302 = gda_sql_expr_new (NULL);
					       cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy302));
					       yygotominor.yy302->cond = cond;
					       cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOTIN;
					       cond->operands = g_slist_prepend (g_slist_reverse (yymsp[-1].minor.yy33), yymsp[-5].minor.yy302);
					       for (list = cond->operands; list; list = list->next)
						       GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,68,&yymsp[-4].minor);
  yy_destructor(yypParser,71,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3310 "parser.c"
        break;
      case 168: /* expr ::= expr NOT IN LP compound RP */
#line 899 "./parser.y"
{GdaSqlOperation *cond;
					       GdaSqlExpr *expr;
					       yygotominor.yy302 = gda_sql_expr_new (NULL);
					       cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy302));
					       yygotominor.yy302->cond = cond;
					       cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOTIN;
					       
					       expr = gda_sql_expr_new (GDA_SQL_ANY_PART (cond));
					       gda_sql_expr_take_select (expr, yymsp[-1].minor.yy252);
					       cond->operands = g_slist_prepend (NULL, expr);
					       cond->operands = g_slist_prepend (cond->operands, yymsp[-5].minor.yy302);
					       GDA_SQL_ANY_PART (yymsp[-5].minor.yy302)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,68,&yymsp[-4].minor);
  yy_destructor(yypParser,71,&yymsp[-3].minor);
  yy_destructor(yypParser,101,&yymsp[-2].minor);
  yy_destructor(yypParser,102,&yymsp[0].minor);
}
#line 3331 "parser.c"
        break;
      case 169: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 912 "./parser.y"
{
	GdaSqlCase *sc;
	GSList *list;
	yygotominor.yy302 = gda_sql_expr_new (NULL);
	sc = gda_sql_case_new (GDA_SQL_ANY_PART (yygotominor.yy302));
	sc->base_expr = yymsp[-3].minor.yy354;
	sc->else_expr = yymsp[-1].minor.yy354;
	sc->when_expr_list = yymsp[-2].minor.yy51.when_list;
	sc->then_expr_list = yymsp[-2].minor.yy51.then_list;
	yygotominor.yy302->case_s = sc;
	for (list = sc->when_expr_list; list; list = list->next)
		GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (sc);
	for (list = sc->then_expr_list; list; list = list->next)
		GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (sc);
  yy_destructor(yypParser,144,&yymsp[-4].minor);
  yy_destructor(yypParser,17,&yymsp[0].minor);
}
#line 3352 "parser.c"
        break;
      case 170: /* case_operand ::= expr */
#line 930 "./parser.y"
{yygotominor.yy354 = yymsp[0].minor.yy302;}
#line 3357 "parser.c"
        break;
      case 171: /* case_operand ::= */
      case 175: /* case_else ::= */ yytestcase(yyruleno==175);
#line 931 "./parser.y"
{yygotominor.yy354 = NULL;}
#line 3363 "parser.c"
        break;
      case 172: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 937 "./parser.y"
{
	yygotominor.yy51.when_list = g_slist_append (yymsp[-4].minor.yy51.when_list, yymsp[-2].minor.yy302);
	yygotominor.yy51.then_list = g_slist_append (yymsp[-4].minor.yy51.then_list, yymsp[0].minor.yy302);
  yy_destructor(yypParser,145,&yymsp[-3].minor);
  yy_destructor(yypParser,146,&yymsp[-1].minor);
}
#line 3373 "parser.c"
        break;
      case 173: /* case_exprlist ::= WHEN expr THEN expr */
#line 941 "./parser.y"
{
	yygotominor.yy51.when_list = g_slist_prepend (NULL, yymsp[-2].minor.yy302);
	yygotominor.yy51.then_list = g_slist_prepend (NULL, yymsp[0].minor.yy302);
  yy_destructor(yypParser,145,&yymsp[-3].minor);
  yy_destructor(yypParser,146,&yymsp[-1].minor);
}
#line 3383 "parser.c"
        break;
      case 174: /* case_else ::= ELSE expr */
#line 948 "./parser.y"
{yygotominor.yy354 = yymsp[0].minor.yy302;  yy_destructor(yypParser,147,&yymsp[-1].minor);
}
#line 3389 "parser.c"
        break;
      case 176: /* uni_op ::= ISNULL */
#line 952 "./parser.y"
{yygotominor.yy295 = GDA_SQL_OPERATOR_TYPE_ISNULL;  yy_destructor(yypParser,72,&yymsp[0].minor);
}
#line 3395 "parser.c"
        break;
      case 177: /* uni_op ::= IS NOTNULL */
#line 953 "./parser.y"
{yygotominor.yy295 = GDA_SQL_OPERATOR_TYPE_ISNOTNULL;  yy_destructor(yypParser,69,&yymsp[-1].minor);
  yy_destructor(yypParser,73,&yymsp[0].minor);
}
#line 3402 "parser.c"
        break;
      case 178: /* value ::= NULL */
#line 957 "./parser.y"
{yygotominor.yy0 = NULL;  yy_destructor(yypParser,148,&yymsp[0].minor);
}
#line 3408 "parser.c"
        break;
      case 182: /* pvalue ::= UNSPECVAL LSBRACKET paramspec RSBRACKET */
#line 966 "./parser.y"
{yygotominor.yy302 = gda_sql_expr_new (NULL); yygotominor.yy302->param_spec = yymsp[-1].minor.yy303;  yy_destructor(yypParser,150,&yymsp[-3].minor);
  yy_destructor(yypParser,151,&yymsp[-2].minor);
  yy_destructor(yypParser,152,&yymsp[0].minor);
}
#line 3416 "parser.c"
        break;
      case 183: /* pvalue ::= value LSBRACKET paramspec RSBRACKET */
#line 967 "./parser.y"
{yygotominor.yy302 = gda_sql_expr_new (NULL); yygotominor.yy302->value = yymsp[-3].minor.yy0; yygotominor.yy302->param_spec = yymsp[-1].minor.yy303;  yy_destructor(yypParser,151,&yymsp[-2].minor);
  yy_destructor(yypParser,152,&yymsp[0].minor);
}
#line 3423 "parser.c"
        break;
      case 184: /* pvalue ::= SIMPLEPARAM */
#line 968 "./parser.y"
{yygotominor.yy302 = gda_sql_expr_new (NULL); yygotominor.yy302->param_spec = gda_sql_param_spec_new (yymsp[0].minor.yy0);}
#line 3428 "parser.c"
        break;
      case 185: /* paramspec ::= */
#line 973 "./parser.y"
{yygotominor.yy303 = NULL;}
#line 3433 "parser.c"
        break;
      case 186: /* paramspec ::= paramspec PNAME */
#line 974 "./parser.y"
{if (!yymsp[-1].minor.yy303) yygotominor.yy303 = gda_sql_param_spec_new (NULL); else yygotominor.yy303 = yymsp[-1].minor.yy303; 
					 gda_sql_param_spec_take_name (yygotominor.yy303, yymsp[0].minor.yy0);}
#line 3439 "parser.c"
        break;
      case 187: /* paramspec ::= paramspec PDESCR */
#line 976 "./parser.y"
{if (!yymsp[-1].minor.yy303) yygotominor.yy303 = gda_sql_param_spec_new (NULL); else yygotominor.yy303 = yymsp[-1].minor.yy303; 
					 gda_sql_param_spec_take_descr (yygotominor.yy303, yymsp[0].minor.yy0);}
#line 3445 "parser.c"
        break;
      case 188: /* paramspec ::= paramspec PTYPE */
#line 978 "./parser.y"
{if (!yymsp[-1].minor.yy303) yygotominor.yy303 = gda_sql_param_spec_new (NULL); else yygotominor.yy303 = yymsp[-1].minor.yy303; 
					 gda_sql_param_spec_take_type (yygotominor.yy303, yymsp[0].minor.yy0);}
#line 3451 "parser.c"
        break;
      case 189: /* paramspec ::= paramspec PNULLOK */
#line 980 "./parser.y"
{if (!yymsp[-1].minor.yy303) yygotominor.yy303 = gda_sql_param_spec_new (NULL); else yygotominor.yy303 = yymsp[-1].minor.yy303; 
					   gda_sql_param_spec_take_nullok (yygotominor.yy303, yymsp[0].minor.yy0);}
#line 3457 "parser.c"
        break;
      default:
      /* (34) trans_opt_kw ::= */ yytestcase(yyruleno==34);
      /* (36) opt_comma ::= */ yytestcase(yyruleno==36);
        break;
  };
  yygoto = yyRuleInfo[yyruleno].lhs;
  yysize = yyRuleInfo[yyruleno].nrhs;
  yypParser->yyidx -= yysize;
  yyact = yy_find_reduce_action(yymsp[-yysize].stateno,(YYCODETYPE)yygoto);
  if( yyact < YYNSTATE ){
#ifdef NDEBUG
    /* If we are not debugging and the reduce action popped at least
    ** one element off the stack, then we can push the new element back
    ** onto the stack here, and skip the stack overflow test in yy_shift().
    ** That gives a significant speed improvement. */
    if( yysize ){
      yypParser->yyidx++;
      yymsp -= yysize-1;
      yymsp->stateno = (YYACTIONTYPE)yyact;
      yymsp->major = (YYCODETYPE)yygoto;
      yymsp->minor = yygotominor;
    }else
#endif
    {
      yy_shift(yypParser,yyact,yygoto,&yygotominor);
    }
  }else{
    assert( yyact == YYNSTATE + YYNRULE + 1 );
    yy_accept(yypParser);
  }
}

/*
** The following code executes when the parse fails
*/
#ifndef YYNOERRORRECOVERY
static void yy_parse_failed(
  yyParser *yypParser           /* The parser */
){
  gda_lemon_oracle_parserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
  gda_lemon_oracle_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}
#endif /* YYNOERRORRECOVERY */

/*
** The following code executes when a syntax error first occurs.
*/
static void yy_syntax_error(
  yyParser *yypParser,           /* The parser */
  int yymajor,                   /* The major type of the error token */
  YYMINORTYPE yyminor            /* The minor type of the error token */
){
  gda_lemon_oracle_parserARG_FETCH;
#define TOKEN (yyminor.yy0)
#line 22 "./parser.y"

	gda_sql_parser_set_syntax_error (pdata->parser);
#line 3524 "parser.c"
  gda_lemon_oracle_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  gda_lemon_oracle_parserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
  gda_lemon_oracle_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "gda_lemon_oracle_parserAlloc" which describes the current state of the parser.
** The second argument is the major token number.  The third is
** the minor token.  The fourth optional argument is whatever the
** user wants (and specified in the grammar) and is available for
** use by the action routines.
**
** Inputs:
** <ul>
** <li> A pointer to the parser (an opaque structure.)
** <li> The major token number.
** <li> The minor token number.
** <li> An option argument of a grammar-specified type.
** </ul>
**
** Outputs:
** None.
*/
void gda_lemon_oracle_parser(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  gda_lemon_oracle_parserTOKENTYPE yyminor       /* The value for the token */
  gda_lemon_oracle_parserARG_PDECL               /* Optional %extra_argument parameter */
){
  YYMINORTYPE yyminorunion;
  int yyact;            /* The parser action. */
  int yyendofinput;     /* True if we are at the end of input */
#ifdef YYERRORSYMBOL
  int yyerrorhit = 0;   /* True if yymajor has invoked an error */
#endif
  yyParser *yypParser;  /* The parser */

  /* (re)initialize the parser, if necessary */
  yypParser = (yyParser*)yyp;
  if( yypParser->yyidx<0 ){
#if YYSTACKDEPTH<=0
    if( yypParser->yystksz <=0 ){
      /*memset(&yyminorunion, 0, sizeof(yyminorunion));*/
      yyminorunion = yyzerominor;
      yyStackOverflow(yypParser, &yyminorunion);
      return;
    }
#endif
    yypParser->yyidx = 0;
    yypParser->yyerrcnt = -1;
    yypParser->yystack[0].stateno = 0;
    yypParser->yystack[0].major = 0;
  }
  yyminorunion.yy0 = yyminor;
  yyendofinput = (yymajor==0);
  gda_lemon_oracle_parserARG_STORE;

#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sInput %s\n",yyTracePrompt,yyTokenName[yymajor]);
  }
#endif

  do{
    yyact = yy_find_shift_action(yypParser,(YYCODETYPE)yymajor);
    if( yyact<YYNSTATE ){
      assert( !yyendofinput );  /* Impossible to shift the $ token */
      yy_shift(yypParser,yyact,yymajor,&yyminorunion);
      yypParser->yyerrcnt--;
      yymajor = YYNOCODE;
    }else if( yyact < YYNSTATE + YYNRULE ){
      yy_reduce(yypParser,yyact-YYNSTATE);
    }else{
      assert( yyact == YY_ERROR_ACTION );
#ifdef YYERRORSYMBOL
      int yymx;
#endif
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE,"%sSyntax Error!\n",yyTracePrompt);
      }
#endif
#ifdef YYERRORSYMBOL
      /* A syntax error has occurred.
      ** The response to an error depends upon whether or not the
      ** grammar defines an error token "ERROR".  
      **
      ** This is what we do if the grammar does define ERROR:
      **
      **  * Call the %syntax_error function.
      **
      **  * Begin popping the stack until we enter a state where
      **    it is legal to shift the error symbol, then shift
      **    the error symbol.
      **
      **  * Set the error count to three.
      **
      **  * Begin accepting and shifting new tokens.  No new error
      **    processing will occur until three tokens have been
      **    shifted successfully.
      **
      */
      if( yypParser->yyerrcnt<0 ){
        yy_syntax_error(yypParser,yymajor,yyminorunion);
      }
      yymx = yypParser->yystack[yypParser->yyidx].major;
      if( yymx==YYERRORSYMBOL || yyerrorhit ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE,"%sDiscard input token %s\n",
             yyTracePrompt,yyTokenName[yymajor]);
        }
#endif
        yy_destructor(yypParser, (YYCODETYPE)yymajor,&yyminorunion);
        yymajor = YYNOCODE;
      }else{
         while(
          yypParser->yyidx >= 0 &&
          yymx != YYERRORSYMBOL &&
          (yyact = yy_find_reduce_action(
                        yypParser->yystack[yypParser->yyidx].stateno,
                        YYERRORSYMBOL)) >= YYNSTATE
        ){
          yy_pop_parser_stack(yypParser);
        }
        if( yypParser->yyidx < 0 || yymajor==0 ){
          yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
          yy_parse_failed(yypParser);
          yymajor = YYNOCODE;
        }else if( yymx!=YYERRORSYMBOL ){
          YYMINORTYPE u2;
          u2.YYERRSYMDT = 0;
          yy_shift(yypParser,yyact,YYERRORSYMBOL,&u2);
        }
      }
      yypParser->yyerrcnt = 3;
      yyerrorhit = 1;
#elif defined(YYNOERRORRECOVERY)
      /* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
      ** do any kind of error recovery.  Instead, simply invoke the syntax
      ** error routine and continue going as if nothing had happened.
      **
      ** Applications can set this macro (for example inside %include) if
      ** they intend to abandon the parse upon the first syntax error seen.
      */
      yy_syntax_error(yypParser,yymajor,yyminorunion);
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      yymajor = YYNOCODE;
      
#else  /* YYERRORSYMBOL is not defined */
      /* This is what we do if the grammar does not define ERROR:
      **
      **  * Report an error message, and throw away the input token.
      **
      **  * If the input token is $, then fail the parse.
      **
      ** As before, subsequent error messages are suppressed until
      ** three input tokens have been successfully shifted.
      */
      if( yypParser->yyerrcnt<=0 ){
        yy_syntax_error(yypParser,yymajor,yyminorunion);
      }
      yypParser->yyerrcnt = 3;
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      if( yyendofinput ){
        yy_parse_failed(yypParser);
      }
      yymajor = YYNOCODE;
#endif
    }
  }while( yymajor!=YYNOCODE && yypParser->yyidx>=0 );
  return;
}
