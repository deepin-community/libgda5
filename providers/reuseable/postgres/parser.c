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
		else if (op[1] == 'I')
			return GDA_SQL_OPERATOR_TYPE_ILIKE;
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

#line 208 "parser.c"
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
**    gda_lemon_postgres_parserTOKENTYPE     is the data type used for minor tokens given 
**                       directly to the parser from the tokenizer.
**    YYMINORTYPE        is the data type used for all minor tokens.
**                       This is typically a union of many types, one of
**                       which is gda_lemon_postgres_parserTOKENTYPE.  The entry in the union
**                       for base tokens is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    gda_lemon_postgres_parserARG_SDECL     A static variable declaration for the %extra_argument
**    gda_lemon_postgres_parserARG_PDECL     A parameter declaration for the %extra_argument
**    gda_lemon_postgres_parserARG_STORE     Code to store %extra_argument into yypParser
**    gda_lemon_postgres_parserARG_FETCH     Code to extract %extra_argument from yypParser
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
*/
#define YYCODETYPE unsigned char
#define YYNOCODE 211
#define YYACTIONTYPE unsigned short int
#define gda_lemon_postgres_parserTOKENTYPE GValue *
typedef union {
  int yyinit;
  gda_lemon_postgres_parserTOKENTYPE yy0;
  Limit yy44;
  CaseBody yy59;
  GdaSqlExpr * yy70;
  GdaSqlStatement * yy116;
  GdaSqlSelectTarget * yy134;
  GdaSqlExpr* yy146;
  GdaSqlOperatorType yy147;
  GdaTransactionIsolation yy169;
  GdaSqlSelectFrom * yy191;
  gboolean yy276;
  Distinct * yy297;
  GSList * yy325;
  GSList* yy333;
  GdaSqlParamSpec * yy339;
  GdaSqlSelectJoinType yy371;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define gda_lemon_postgres_parserARG_SDECL GdaSqlParserIface *pdata;
#define gda_lemon_postgres_parserARG_PDECL ,GdaSqlParserIface *pdata
#define gda_lemon_postgres_parserARG_FETCH GdaSqlParserIface *pdata = yypParser->pdata
#define gda_lemon_postgres_parserARG_STORE yypParser->pdata = pdata
#define YYNSTATE 365
#define YYNRULE 198
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
#define YY_ACTTAB_COUNT (1412)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    41,   40,  247,  171,  244,  564,  147,   22,  125,   28,
 /*    10 */   103,   26,   36,   43,   43,   43,   43,   37,   37,   37,
 /*    20 */    37,   37,  365,   47,   47,  361,  366,   52,   52,   51,
 /*    30 */    49,   49,   44,  245,  354,  353,  165,  352,  351,  350,
 /*    40 */   349,   45,   46,  203,   13,   39,   38,  239,  339,   78,
 /*    50 */    42,   42,   36,   43,   43,   43,   43,   37,   37,   37,
 /*    60 */    37,   37,  357,   47,   47,  323,   82,   52,   52,   51,
 /*    70 */    49,   49,   44,  328,  322,  318,  229,  228,  200,  199,
 /*    80 */   198,  230,  293,  140,  131,  362,   41,   40,  280,   78,
 /*    90 */   213,   50,  319,  173,  172,  171,   21,  146,  138,   43,
 /*   100 */    43,   43,   43,   37,   37,   37,   37,   37,  137,   47,
 /*   110 */    47,  314,  184,   52,   52,   51,   49,   49,   44,  178,
 /*   120 */   173,  172,  171,  215,  218,  288,  287,   45,   46,  203,
 /*   130 */    13,   39,   38,  239,  339,   78,   42,   42,   36,   43,
 /*   140 */    43,   43,   43,   37,   37,   37,   37,   37,  227,   47,
 /*   150 */    47,  282,  281,   52,   52,   51,   49,   49,   44,   41,
 /*   160 */    40,  247,   37,  305,   47,   47,  358,  197,   52,   52,
 /*   170 */    51,   49,   49,   44,  175,   78,   51,   49,   49,   44,
 /*   180 */     1,   73,  333,  273,  352,  351,  350,  349,  263,  280,
 /*   190 */    78,  173,  172,  171,  236,  324,   78,  364,  363,   34,
 /*   200 */    45,   46,  203,   13,   39,   38,  239,  339,  342,   42,
 /*   210 */    42,   36,   43,   43,   43,   43,   37,   37,   37,   37,
 /*   220 */    37,  356,   47,   47,  338,  312,   52,   52,   51,   49,
 /*   230 */    49,   44,   41,   40,  173,  172,  171,  348,  133,   37,
 /*   240 */    37,   37,   37,   37,  357,   47,   47,  303,   78,   52,
 /*   250 */    52,   51,   49,   49,   44,  368,  291,  269,  268,  347,
 /*   260 */   296,  289,  226,  368,  368,  368,  290,  173,  172,  171,
 /*   270 */    48,   78,  223,   45,   46,  203,   13,   39,   38,  239,
 /*   280 */   339,  132,   42,   42,   36,   43,   43,   43,   43,   37,
 /*   290 */    37,   37,   37,   37,  220,   47,   47,  196,  194,   52,
 /*   300 */    52,   51,   49,   49,   44,  183,  340,  247,  182,  305,
 /*   310 */    41,   40,  243,  189,  244,  247,  204,  244,  357,  202,
 /*   320 */   104,   78,  361,  104,   50,  361,  341,   52,   52,   51,
 /*   330 */    49,   49,   44,  343,  173,  172,  171,   68,  310,  265,
 /*   340 */   264,  193,  235,  245,  354,   53,  245,  354,  358,   78,
 /*   350 */    77,   45,   46,  203,   13,   39,   38,  239,  339,  357,
 /*   360 */    42,   42,   36,   43,   43,   43,   43,   37,   37,   37,
 /*   370 */    37,   37,  357,   47,   47,  225,  329,   52,   52,   51,
 /*   380 */    49,   49,   44,   41,   40,  247,  247,  244,  205,  247,
 /*   390 */    57,  244,  330,   63,   50,  139,    3,   95,  326,   78,
 /*   400 */   173,  172,  171,  356,  233,  158,  173,  172,  171,  401,
 /*   410 */   231,  174,  345,  173,  172,  171,  245,  354,  267,  266,
 /*   420 */   245,  354,  358,    2,   45,   46,  203,   13,   39,   38,
 /*   430 */   239,  339,   27,   42,   42,   36,   43,   43,   43,   43,
 /*   440 */    37,   37,   37,   37,   37,  145,   47,   47,  364,  363,
 /*   450 */    52,   52,   51,   49,   49,   44,   41,   40,  247,  357,
 /*   460 */   244,    6,  192,  358,  284,  195,  104,  376,  219,  375,
 /*   470 */   306,  247,   78,  141,   44,   11,  358,  356,   75,  357,
 /*   480 */   286,  376,  376,  375,  375,  286,   10,  286,   76,  245,
 /*   490 */   354,   78,  336,  327,  335,  160,   20,   45,   46,  203,
 /*   500 */    13,   39,   38,  239,  339,   24,   42,   42,   36,   43,
 /*   510 */    43,   43,   43,   37,   37,   37,   37,   37,  356,   47,
 /*   520 */    47,  337,  334,   52,   52,   51,   49,   49,   44,  181,
 /*   530 */   357,  356,  180,  317,   41,   40,  247,   18,  244,  247,
 /*   540 */   201,  244,  222,  150,  104,   78,  361,   90,  278,  361,
 /*   550 */   221,  156,  186,  344,  280,  291,  401,  299,  316,  361,
 /*   560 */   289,   50,   62,  358,   84,  290,  315,  245,  354,  361,
 /*   570 */   245,  354,  122,   59,   57,   45,   46,  203,   13,   39,
 /*   580 */    38,  239,  339,  358,   42,   42,   36,   43,   43,   43,
 /*   590 */    43,   37,   37,   37,   37,   37,  196,   47,   47,   82,
 /*   600 */   313,   52,   52,   51,   49,   49,   44,   41,   40,  247,
 /*   610 */   256,  244,  247,  191,  244,  255,  190,  104,  356,  254,
 /*   620 */   104,  311,  186,   78,  247,  248,  359,  286,   50,  360,
 /*   630 */   146,   16,   79,   50,  358,   58,  129,   76,  356,  361,
 /*   640 */   245,  354,  361,  245,  354,  247,  361,  240,   45,   35,
 /*   650 */   203,   13,   39,   38,  239,  339,  357,   42,   42,   36,
 /*   660 */    43,   43,   43,   43,   37,   37,   37,   37,   37,  309,
 /*   670 */    47,   47,  294,  188,   52,   52,   51,   49,   49,   44,
 /*   680 */    41,   40,  243,  283,  244,  247,  357,  244,  179,  356,
 /*   690 */    65,  279,  247,  108,  307,  217,   78,  280,  247,  216,
 /*   700 */   302,  275,  272,  271,  247,  361,  151,  280,  280,  247,
 /*   710 */    72,  149,  297,  245,  354,  136,  245,  354,  247,  154,
 /*   720 */   207,   45,   33,  203,   13,   39,   38,  239,  339,  301,
 /*   730 */    42,   42,   36,   43,   43,   43,   43,   37,   37,   37,
 /*   740 */    37,   37,  357,   47,   47,  134,    9,   52,   52,   51,
 /*   750 */    49,   49,   44,   41,   40,  185,  187,  247,  211,  244,
 /*   760 */   358,  247,  247,  206,  244,  124,  298,  321,  262,   78,
 /*   770 */    90,  342,  361,  361,  280,    8,    7,  253,   70,   60,
 /*   780 */   342,   15,   14,   83,  295,  177,   81,  176,  245,  354,
 /*   790 */   358,   80,  164,  245,  354,   46,  203,   13,   39,   38,
 /*   800 */   239,  339,  342,   42,   42,   36,   43,   43,   43,   43,
 /*   810 */    37,   37,   37,   37,   37,  356,   47,   47,   53,  144,
 /*   820 */    52,   52,   51,   49,   49,   44,   41,   40,  247,   78,
 /*   830 */   244,  247,  163,  244,   25,  247,  142,  244,  232,  107,
 /*   840 */   162,   19,   78,  143,  357,  356,  358,  161,  234,   17,
 /*   850 */   325,   23,  285,  241,  159,  224,  220,   57,  277,  245,
 /*   860 */   354,  300,  245,  354,  308,  127,  245,  354,   74,  203,
 /*   870 */    13,   39,   38,  239,  339,  274,   42,   42,   36,   43,
 /*   880 */    43,   43,   43,   37,   37,   37,   37,   37,  270,   47,
 /*   890 */    47,   66,  178,   52,   52,   51,   49,   49,   44,   67,
 /*   900 */   155,  356,  209,  247,  210,  244,  214,   71,  247,   61,
 /*   910 */   244,   89,   30,  252,  357,   78,  123,  212,   69,   85,
 /*   920 */   247,  250,  244,  241,  170,  304,  238,  157,  112,  251,
 /*   930 */   130,  246,   56,  153,  245,  354,   31,   32,  346,  245,
 /*   940 */   354,  355,  260,  135,  126,   29,    4,  259,  358,  258,
 /*   950 */   242,  245,  354,  292,  128,  276,  261,  249,  565,  257,
 /*   960 */   565,  320,  565,  565,  336,  565,  335,  565,  565,  565,
 /*   970 */   565,  565,  565,  247,  565,  244,  565,  146,  565,  565,
 /*   980 */   565,  109,   30,  565,  565,  565,   55,  565,  331,   12,
 /*   990 */   357,  291,  565,  337,  334,  237,  289,  565,  332,  241,
 /*  1000 */   565,  290,  565,  356,  245,  354,   31,   32,  152,  565,
 /*  1010 */   565,  208,  565,  565,  148,   29,    5,  146,  358,  565,
 /*  1020 */   565,  565,  565,  565,  565,  565,  565,  565,  565,  220,
 /*  1030 */   565,  565,  196,  565,  336,  247,  335,  244,  565,  247,
 /*  1040 */   565,  244,  565,  169,  247,  565,  244,  120,  565,  247,
 /*  1050 */   565,  244,  118,  565,  565,  565,  565,  117,   30,   12,
 /*  1060 */   357,  565,  565,  337,  334,  237,  245,  354,  332,  241,
 /*  1070 */   245,  354,  565,  356,  565,  245,  354,  565,  565,  565,
 /*  1080 */   245,  354,   31,   32,  346,  565,  357,  565,  247,  565,
 /*  1090 */   244,   29,    5,  565,  358,  241,  116,  565,   54,  565,
 /*  1100 */   565,  247,  565,  244,  565,  247,  565,  244,  565,  115,
 /*  1110 */   336,  565,  335,  114,  565,  565,  247,  565,  244,  245,
 /*  1120 */   354,  565,  565,  565,  121,  565,  565,  565,   30,  565,
 /*  1130 */   565,  565,  245,  354,  565,   12,  245,  354,  565,  337,
 /*  1140 */   334,  237,  565,  565,  332,  565,  565,  245,  354,  356,
 /*  1150 */   565,  565,   31,   32,   30,  565,  565,  565,  565,  565,
 /*  1160 */   565,   29,    4,  247,  358,  244,  247,  565,  244,  565,
 /*  1170 */   565,  106,  565,  565,  119,  565,  565,  565,   31,   32,
 /*  1180 */   336,  247,  335,  244,  247,  565,  244,   29,    5,  105,
 /*  1190 */   358,  565,  111,  146,  245,  354,  565,  245,  354,  565,
 /*  1200 */   565,  565,  565,  565,  565,   12,  336,  565,  335,  337,
 /*  1210 */   334,  237,  245,  354,  332,  245,  354,  565,  565,  356,
 /*  1220 */   247,  565,  244,  565,  247,  565,  244,  565,  168,  565,
 /*  1230 */   565,   12,  167,  565,  565,  337,  334,  237,  565,  247,
 /*  1240 */   332,  244,  247,  565,  244,  356,  247,  110,  244,  565,
 /*  1250 */   166,  245,  354,  565,   88,  245,  354,  565,  565,  247,
 /*  1260 */   565,  244,  565,  565,  247,  565,  244,  102,  565,  565,
 /*  1270 */   245,  354,  101,  245,  354,  565,  565,  245,  354,  565,
 /*  1280 */   247,  565,  244,  565,  565,  247,  565,  244,   87,  565,
 /*  1290 */   245,  354,  247,  100,  244,  245,  354,  247,  565,  244,
 /*  1300 */    86,  247,  565,  244,  565,   99,  247,  565,  244,   98,
 /*  1310 */   565,  245,  354,  247,   64,  244,  245,  354,  565,  565,
 /*  1320 */   565,   97,  565,  245,  354,  565,  565,  565,  245,  354,
 /*  1330 */   565,  565,  245,  354,  247,  565,  244,  245,  354,  247,
 /*  1340 */   565,  244,   96,  565,  245,  354,  565,   94,  565,  565,
 /*  1350 */   565,  247,  565,  244,  247,  565,  244,  565,  565,   93,
 /*  1360 */   565,  247,   92,  244,  565,  245,  354,  565,  565,   91,
 /*  1370 */   245,  354,  565,  565,  565,  565,  565,  565,  565,  247,
 /*  1380 */   565,  244,  245,  354,  565,  245,  354,  113,  565,  565,
 /*  1390 */   565,  565,  245,  354,  565,  565,  565,  565,  565,  565,
 /*  1400 */   565,  565,  565,  565,  565,  565,  565,  565,  565,  565,
 /*  1410 */   245,  354,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */    26,   27,  172,  114,  174,  162,  163,   33,  165,  147,
 /*    10 */   180,  149,   78,   79,   80,   81,   82,   83,   84,   85,
 /*    20 */    86,   87,    0,   89,   90,  182,    0,   93,   94,   95,
 /*    30 */    96,   97,   98,  203,  204,  154,  206,  156,  157,  158,
 /*    40 */   159,   67,   68,   69,   70,   71,   72,   73,   74,  115,
 /*    50 */    76,   77,   78,   79,   80,   81,   82,   83,   84,   85,
 /*    60 */    86,   87,    1,   89,   90,    5,  103,   93,   94,   95,
 /*    70 */    96,   97,   98,  104,   14,  105,  106,  107,  108,  109,
 /*    80 */   110,  111,  166,  167,  168,  104,   26,   27,  172,  115,
 /*    90 */   127,  122,  122,  112,  113,  114,  122,  134,  143,   79,
 /*   100 */    80,   81,   82,   83,   84,   85,   86,   87,  143,   89,
 /*   110 */    90,  105,  104,   93,   94,   95,   96,   97,   98,   58,
 /*   120 */   112,  113,  114,   62,   63,   58,   59,   67,   68,   69,
 /*   130 */    70,   71,   72,   73,   74,  115,   76,   77,   78,   79,
 /*   140 */    80,   81,   82,   83,   84,   85,   86,   87,  142,   89,
 /*   150 */    90,   55,   56,   93,   94,   95,   96,   97,   98,   26,
 /*   160 */    27,  172,   87,  174,   89,   90,  105,  178,   93,   94,
 /*   170 */    95,   96,   97,   98,  153,  115,   95,   96,   97,   98,
 /*   180 */   103,  120,  154,  166,  156,  157,  158,  159,  171,  172,
 /*   190 */   115,  112,  113,  114,   73,  192,  115,  118,  119,   78,
 /*   200 */    67,   68,   69,   70,   71,   72,   73,   74,  205,   76,
 /*   210 */    77,   78,   79,   80,   81,   82,   83,   84,   85,   86,
 /*   220 */    87,  160,   89,   90,   17,  105,   93,   94,   95,   96,
 /*   230 */    97,   98,   26,   27,  112,  113,  114,  104,  143,   83,
 /*   240 */    84,   85,   86,   87,    1,   89,   90,    1,  115,   93,
 /*   250 */    94,   95,   96,   97,   98,  104,   13,   64,   65,  104,
 /*   260 */   133,   18,  142,  112,  113,  114,   23,  112,  113,  114,
 /*   270 */   103,  115,  145,   67,   68,   69,   70,   71,   72,   73,
 /*   280 */    74,  143,   76,   77,   78,   79,   80,   81,   82,   83,
 /*   290 */    84,   85,   86,   87,   51,   89,   90,   54,   23,   93,
 /*   300 */    94,   95,   96,   97,   98,  165,  104,  172,  165,  174,
 /*   310 */    26,   27,  172,  178,  174,  172,  176,  174,    1,  176,
 /*   320 */   180,  115,  182,  180,  122,  182,  104,   93,   94,   95,
 /*   330 */    96,   97,   98,  104,  112,  113,  114,  122,  105,   64,
 /*   340 */    65,   66,  202,  203,  204,  130,  203,  204,  105,  115,
 /*   350 */   144,   67,   68,   69,   70,   71,   72,   73,   74,    1,
 /*   360 */    76,   77,   78,   79,   80,   81,   82,   83,   84,   85,
 /*   370 */    86,   87,    1,   89,   90,  142,  104,   93,   94,   95,
 /*   380 */    96,   97,   98,   26,   27,  172,  172,  174,  174,  172,
 /*   390 */   144,  174,  104,  180,  122,  181,  103,  180,  104,  115,
 /*   400 */   112,  113,  114,  160,  191,  104,  112,  113,  114,   51,
 /*   410 */   193,  153,   95,  112,  113,  114,  203,  204,   64,   65,
 /*   420 */   203,  204,  105,  103,   67,   68,   69,   70,   71,   72,
 /*   430 */    73,   74,  148,   76,   77,   78,   79,   80,   81,   82,
 /*   440 */    83,   84,   85,   86,   87,  185,   89,   90,  118,  119,
 /*   450 */    93,   94,   95,   96,   97,   98,   26,   27,  172,    1,
 /*   460 */   174,  201,  176,  105,   53,   54,  180,  104,   57,  104,
 /*   470 */   104,  172,  115,  174,   98,  137,  105,  160,  120,    1,
 /*   480 */   122,  118,  119,  118,  119,  122,  137,  122,  122,  203,
 /*   490 */   204,  115,  121,  104,  123,  196,  122,   67,   68,   69,
 /*   500 */    70,   71,   72,   73,   74,  148,   76,   77,   78,   79,
 /*   510 */    80,   81,   82,   83,   84,   85,   86,   87,  160,   89,
 /*   520 */    90,  150,  151,   93,   94,   95,   96,   97,   98,  165,
 /*   530 */     1,  160,  165,  105,   26,   27,  172,  122,  174,  172,
 /*   540 */   176,  174,  165,  104,  180,  115,  182,  180,  166,  182,
 /*   550 */   163,  169,  165,   95,  172,   13,   51,  122,  105,  182,
 /*   560 */    18,  122,  103,  105,  129,   23,  105,  203,  204,  182,
 /*   570 */   203,  204,  194,  195,  144,   67,   68,   69,   70,   71,
 /*   580 */    72,   73,   74,  105,   76,   77,   78,   79,   80,   81,
 /*   590 */    82,   83,   84,   85,   86,   87,   54,   89,   90,  103,
 /*   600 */   105,   93,   94,   95,   96,   97,   98,   26,   27,  172,
 /*   610 */   104,  174,  172,  176,  174,  104,  176,  180,  160,  104,
 /*   620 */   180,  105,  165,  115,  172,  165,  174,  122,  122,  165,
 /*   630 */   134,  141,  103,  122,  105,  167,  168,  122,  160,  182,
 /*   640 */   203,  204,  182,  203,  204,  172,  182,  174,   67,   68,
 /*   650 */    69,   70,   71,   72,   73,   74,    1,   76,   77,   78,
 /*   660 */    79,   80,   81,   82,   83,   84,   85,   86,   87,  105,
 /*   670 */    89,   90,  104,   52,   93,   94,   95,   96,   97,   98,
 /*   680 */    26,   27,  172,   54,  174,  172,    1,  174,  165,  160,
 /*   690 */   180,  166,  172,  180,  174,  121,  115,  172,  172,  122,
 /*   700 */   174,  166,  166,  123,  172,  182,  174,  172,  172,  172,
 /*   710 */   126,  174,  202,  203,  204,   60,  203,  204,  172,  121,
 /*   720 */   174,   67,   68,   69,   70,   71,   72,   73,   74,  203,
 /*   730 */    76,   77,   78,   79,   80,   81,   82,   83,   84,   85,
 /*   740 */    86,   87,    1,   89,   90,   60,  103,   93,   94,   95,
 /*   750 */    96,   97,   98,   26,   27,  165,  165,  172,  122,  174,
 /*   760 */   105,  172,  172,  174,  174,  180,  200,  192,  166,  115,
 /*   770 */   180,  205,  182,  182,  172,  103,  103,    1,  129,  132,
 /*   780 */   205,   77,   77,  183,  133,  209,  183,  209,  203,  204,
 /*   790 */   105,  183,  179,  203,  204,   68,   69,   70,   71,   72,
 /*   800 */    73,   74,  205,   76,   77,   78,   79,   80,   81,   82,
 /*   810 */    83,   84,   85,   86,   87,  160,   89,   90,  130,  207,
 /*   820 */    93,   94,   95,   96,   97,   98,   26,   27,  172,  115,
 /*   830 */   174,  172,  187,  174,  147,  172,  180,  174,  139,  180,
 /*   840 */   188,  138,  115,  180,    1,  160,  105,  189,  136,  141,
 /*   850 */   190,  135,  168,   10,  197,  140,   51,  144,  168,  203,
 /*   860 */   204,  200,  203,  204,  198,  124,  203,  204,  170,   69,
 /*   870 */    70,   71,   72,   73,   74,  120,   76,   77,   78,   79,
 /*   880 */    80,   81,   82,   83,   84,   85,   86,   87,  171,   89,
 /*   890 */    90,    8,   58,   93,   94,   95,   96,   97,   98,  170,
 /*   900 */    17,  160,   67,  172,  122,  174,  173,  175,  172,  103,
 /*   910 */   174,  180,   69,  179,    1,  115,  180,  177,  173,  184,
 /*   920 */   172,  164,  174,   10,  186,  200,  208,  169,  180,  179,
 /*   930 */   169,  172,   49,   50,  203,  204,   93,   94,   95,  203,
 /*   940 */   204,  172,  172,   60,   61,  102,  103,  172,  105,  172,
 /*   950 */   172,  203,  204,  167,  169,  167,  172,  164,  210,  172,
 /*   960 */   210,  199,  210,  210,  121,  210,  123,  210,  210,  210,
 /*   970 */   210,  210,  210,  172,  210,  174,  210,  134,  210,  210,
 /*   980 */   210,  180,   69,  210,  210,  210,  103,  210,   75,  146,
 /*   990 */     1,   13,  210,  150,  151,  152,   18,  210,  155,   10,
 /*  1000 */   210,   23,  210,  160,  203,  204,   93,   94,  125,  210,
 /*  1010 */   210,  128,  210,  210,  131,  102,  103,  134,  105,  210,
 /*  1020 */   210,  210,  210,  210,  210,  210,  210,  210,  210,   51,
 /*  1030 */   210,  210,   54,  210,  121,  172,  123,  174,  210,  172,
 /*  1040 */   210,  174,  210,  180,  172,  210,  174,  180,  210,  172,
 /*  1050 */   210,  174,  180,  210,  210,  210,  210,  180,   69,  146,
 /*  1060 */     1,  210,  210,  150,  151,  152,  203,  204,  155,   10,
 /*  1070 */   203,  204,  210,  160,  210,  203,  204,  210,  210,  210,
 /*  1080 */   203,  204,   93,   94,   95,  210,    1,  210,  172,  210,
 /*  1090 */   174,  102,  103,  210,  105,   10,  180,  210,  120,  210,
 /*  1100 */   210,  172,  210,  174,  210,  172,  210,  174,  210,  180,
 /*  1110 */   121,  210,  123,  180,  210,  210,  172,  210,  174,  203,
 /*  1120 */   204,  210,  210,  210,  180,  210,  210,  210,   69,  210,
 /*  1130 */   210,  210,  203,  204,  210,  146,  203,  204,  210,  150,
 /*  1140 */   151,  152,  210,  210,  155,  210,  210,  203,  204,  160,
 /*  1150 */   210,  210,   93,   94,   69,  210,  210,  210,  210,  210,
 /*  1160 */   210,  102,  103,  172,  105,  174,  172,  210,  174,  210,
 /*  1170 */   210,  180,  210,  210,  180,  210,  210,  210,   93,   94,
 /*  1180 */   121,  172,  123,  174,  172,  210,  174,  102,  103,  180,
 /*  1190 */   105,  210,  180,  134,  203,  204,  210,  203,  204,  210,
 /*  1200 */   210,  210,  210,  210,  210,  146,  121,  210,  123,  150,
 /*  1210 */   151,  152,  203,  204,  155,  203,  204,  210,  210,  160,
 /*  1220 */   172,  210,  174,  210,  172,  210,  174,  210,  180,  210,
 /*  1230 */   210,  146,  180,  210,  210,  150,  151,  152,  210,  172,
 /*  1240 */   155,  174,  172,  210,  174,  160,  172,  180,  174,  210,
 /*  1250 */   180,  203,  204,  210,  180,  203,  204,  210,  210,  172,
 /*  1260 */   210,  174,  210,  210,  172,  210,  174,  180,  210,  210,
 /*  1270 */   203,  204,  180,  203,  204,  210,  210,  203,  204,  210,
 /*  1280 */   172,  210,  174,  210,  210,  172,  210,  174,  180,  210,
 /*  1290 */   203,  204,  172,  180,  174,  203,  204,  172,  210,  174,
 /*  1300 */   180,  172,  210,  174,  210,  180,  172,  210,  174,  180,
 /*  1310 */   210,  203,  204,  172,  180,  174,  203,  204,  210,  210,
 /*  1320 */   210,  180,  210,  203,  204,  210,  210,  210,  203,  204,
 /*  1330 */   210,  210,  203,  204,  172,  210,  174,  203,  204,  172,
 /*  1340 */   210,  174,  180,  210,  203,  204,  210,  180,  210,  210,
 /*  1350 */   210,  172,  210,  174,  172,  210,  174,  210,  210,  180,
 /*  1360 */   210,  172,  180,  174,  210,  203,  204,  210,  210,  180,
 /*  1370 */   203,  204,  210,  210,  210,  210,  210,  210,  210,  172,
 /*  1380 */   210,  174,  203,  204,  210,  203,  204,  180,  210,  210,
 /*  1390 */   210,  210,  203,  204,  210,  210,  210,  210,  210,  210,
 /*  1400 */   210,  210,  210,  210,  210,  210,  210,  210,  210,  210,
 /*  1410 */   203,  204,
};
#define YY_SHIFT_USE_DFLT (-139)
#define YY_SHIFT_COUNT (250)
#define YY_SHIFT_MIN   (-138)
#define YY_SHIFT_MAX   (1085)
static const short yy_shift_ofst[] = {
 /*     0 */   883,  843, 1059, 1059, 1059, 1059,  989, 1085, 1085, 1085,
 /*    10 */  1085, 1085, 1085,  913, 1085, 1085, 1085, 1085, 1085, 1085,
 /*    20 */  1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085,
 /*    30 */  1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085,
 /*    40 */  1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085, 1085,
 /*    50 */  1085, 1085, 1085, 1085,  243,  883,   61,  371,  358,  529,
 /*    60 */   478,  478,  478,   60,   60,  430,  978,  741,  478,  478,
 /*    70 */   478,  -37,  478,  478,  478,  478,  478,  478,  478,  496,
 /*    80 */   496,  496,  496,  496, -139, -139,  -26,  357,  284,  206,
 /*    90 */   133,  508,  508,  508,  508,  508,  508,  508,  508,  508,
 /*   100 */   508,  508,  508,  508,  508,  654,  581,  508,  508,  727,
 /*   110 */   800,  800,  800,  -66,  -66,  -66,  -66,  -66,  -66,   20,
 /*   120 */   156,   75,  -30,  234,   81,   79,  685,  655,  542,  365,
 /*   130 */   542,  363,  458,  317,  478,  478,  478,  478,  478,  215,
 /*   140 */   505,  246,  376,  376, -138,  435,  127,  330,  835,  688,
 /*   150 */   782,  806,  835,  755,  834,  755,  805,  805,  713,  715,
 /*   160 */   708,  716,  712,  703,  699,  687,  714,  714,  714,  714,
 /*   170 */   688,  651,  651,  651, -139, -139,   28, -119,  275,  301,
 /*   180 */   294,  288,  222,  155,  151,    8,  -19,  122,  411,  515,
 /*   190 */   511,  506,  439,  354,  193,   96,   67,  366,  233,  120,
 /*   200 */     6,  272,  202,  121,  -31,  705,  704,  647,  649,  776,
 /*   210 */   673,  672,  636,  643,  584,  598,  580,  577,  574,  629,
 /*   220 */   621,  568, -111,  490,  459,  564,  516,  495,  461,  453,
 /*   230 */   428,  415,  349,  374,  338,  389,  320,  258,  207,  293,
 /*   240 */   229,  167,  138,   95,   77,   21,  -35,  -45, -111,   26,
 /*   250 */    22,
};
#define YY_REDUCE_USE_DFLT (-171)
#define YY_REDUCE_COUNT (175)
#define YY_REDUCE_MIN   (-170)
#define YY_REDUCE_MAX   (1207)
static const short yy_reduce_ofst[] = {
 /*     0 */  -157,  140,  364,  143,  590,  367,  510,  440,  437,  286,
 /*    10 */   217,  213, -170, 1207, 1189, 1182, 1179, 1167, 1162, 1141,
 /*    20 */  1134, 1129, 1125, 1120, 1113, 1108, 1092, 1087, 1074, 1070,
 /*    30 */  1067, 1052, 1048, 1012, 1009,  994,  991,  944,  933,  929,
 /*    40 */   916,  877,  872,  867,  863,  801,  748,  736,  731,  663,
 /*    50 */   659,  656,  585,  513,  -84,  387,   17,  526,  382,  299,
 /*    60 */   214,  135,  -11,  575,    3,  566,  468,  602,  589,  546,
 /*    70 */   537,  591,  532,  536,  535,  525,  520,  473,  452,  523,
 /*    80 */   464,  460,  457,  377,  378,  260,  597,  597,  597,  597,
 /*    90 */   597,  597,  597,  597,  597,  597,  597,  597,  597,  597,
 /*   100 */   597,  597,  597,  597,  597,  597,  597,  597,  597,  597,
 /*   110 */   597,  597,  597,  597,  597,  597,  597,  597,  597,  597,
 /*   120 */   597,  597,  762,  597,  597,  793,  787,  784,  788,  785,
 /*   130 */   786,  761,  769,  778,  777,  775,  770,  769,  759,  750,
 /*   140 */   758,  725,  597,  597,  718,  738,  735,  757,  745,  734,
 /*   150 */   740,  732,  733,  729,  717,  698,  690,  684,  661,  666,
 /*   160 */   657,  660,  658,  652,  645,  612,  597,  597,  597,  597,
 /*   170 */   613,  608,  603,  600,  578,  576,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */   563,  497,  497,  497,  563,  563,  563,  497,  497,  497,
 /*    10 */   563,  563,  538,  563,  563,  563,  563,  563,  563,  563,
 /*    20 */   563,  563,  563,  563,  563,  563,  563,  563,  563,  563,
 /*    30 */   563,  563,  563,  563,  563,  563,  563,  563,  563,  563,
 /*    40 */   563,  563,  563,  563,  563,  563,  563,  563,  563,  563,
 /*    50 */   563,  563,  563,  563,  407,  563,  407,  563,  407,  563,
 /*    60 */   563,  563,  563,  453,  453,  490,  371,  407,  563,  563,
 /*    70 */   563,  563,  563,  407,  407,  407,  563,  563,  563,  563,
 /*    80 */   563,  563,  563,  563,  463,  482,  444,  563,  563,  563,
 /*    90 */   563,  435,  434,  494,  465,  496,  495,  455,  446,  445,
 /*   100 */   540,  541,  539,  537,  499,  563,  563,  498,  432,  516,
 /*   110 */   527,  526,  515,  530,  523,  522,  521,  520,  519,  525,
 /*   120 */   518,  524,  459,  512,  509,  563,  563,  563,  563,  401,
 /*   130 */   563,  401,  563,  563,  563,  563,  563,  563,  563,  431,
 /*   140 */   377,  490,  510,  511,  542,  458,  491,  563,  422,  431,
 /*   150 */   419,  426,  422,  399,  387,  399,  563,  563,  490,  462,
 /*   160 */   466,  443,  447,  454,  456,  563,  528,  514,  513,  517,
 /*   170 */   431,  440,  440,  440,  552,  552,  563,  563,  563,  563,
 /*   180 */   563,  563,  563,  563,  531,  563,  563,  421,  563,  563,
 /*   190 */   563,  563,  563,  392,  391,  563,  563,  563,  563,  563,
 /*   200 */   563,  563,  563,  563,  563,  563,  563,  563,  563,  563,
 /*   210 */   563,  563,  420,  563,  563,  563,  563,  385,  563,  563,
 /*   220 */   563,  563,  437,  493,  563,  563,  563,  563,  563,  563,
 /*   230 */   563,  457,  563,  448,  563,  563,  563,  563,  563,  563,
 /*   240 */   563,  563,  561,  560,  503,  501,  561,  560,  438,  563,
 /*   250 */   563,  433,  430,  423,  427,  425,  424,  416,  415,  414,
 /*   260 */   418,  417,  390,  389,  394,  393,  398,  397,  396,  395,
 /*   270 */   388,  386,  384,  383,  400,  382,  381,  380,  374,  373,
 /*   280 */   408,  406,  405,  404,  403,  378,  402,  413,  412,  411,
 /*   290 */   410,  409,  379,  372,  367,  441,  492,  484,  483,  481,
 /*   300 */   480,  489,  488,  479,  478,  429,  461,  428,  460,  477,
 /*   310 */   476,  475,  474,  473,  472,  471,  470,  469,  468,  467,
 /*   320 */   464,  450,  452,  451,  449,  442,  531,  506,  504,  534,
 /*   330 */   535,  544,  551,  549,  548,  547,  546,  545,  536,  543,
 /*   340 */   532,  533,  529,  507,  487,  486,  485,  505,  502,  556,
 /*   350 */   555,  554,  553,  550,  500,  562,  559,  558,  557,  508,
 /*   360 */   439,  436,  368,  370,  369,
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
    1,  /*      ILIKE => ID */
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
  gda_lemon_postgres_parserARG_SDECL                /* A place to hold %extra_argument */
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
void gda_lemon_postgres_parserTrace(FILE *TraceFILE, char *zTracePrompt){
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
  "INITIALLY",     "INSTEAD",       "LIKE",          "ILIKE",       
  "MATCH",         "PLAN",          "QUERY",         "KEY",         
  "OF",            "OFFSET",        "PRAGMA",        "RAISE",       
  "REPLACE",       "RESTRICT",      "ROW",           "TEMP",        
  "TRIGGER",       "VACUUM",        "VIEW",          "VIRTUAL",     
  "REINDEX",       "RENAME",        "CTIME_KW",      "IF",          
  "DELIMITER",     "COMMIT",        "ROLLBACK",      "ISOLATION",   
  "LEVEL",         "SERIALIZABLE",  "READ",          "COMMITTED",   
  "UNCOMMITTED",   "REPEATABLE",    "WRITE",         "ONLY",        
  "SAVEPOINT",     "RELEASE",       "COMMENT",       "FORCE",       
  "WAIT",          "NOWAIT",        "BATCH",         "OR",          
  "AND",           "NOT",           "IS",            "NOTLIKE",     
  "NOTILIKE",      "IN",            "ISNULL",        "NOTNULL",     
  "DIFF",          "EQ",            "BETWEEN",       "GT",          
  "LEQ",           "LT",            "GEQ",           "REGEXP",      
  "REGEXP_CI",     "NOT_REGEXP",    "NOT_REGEXP_CI",  "SIMILAR",     
  "ESCAPE",        "BITAND",        "BITOR",         "LSHIFT",      
  "RSHIFT",        "PLUS",          "MINUS",         "STAR",        
  "SLASH",         "REM",           "CONCAT",        "COLLATE",     
  "UMINUS",        "UPLUS",         "BITNOT",        "LP",          
  "RP",            "JOIN",          "INNER",         "NATURAL",     
  "LEFT",          "RIGHT",         "FULL",          "CROSS",       
  "UNION",         "EXCEPT",        "INTERSECT",     "PGCAST",      
  "ILLEGAL",       "SQLCOMMENT",    "SEMI",          "END_OF_FILE", 
  "TRANSACTION",   "STRING",        "COMMA",         "INTEGER",     
  "TO",            "INSERT",        "INTO",          "VALUES",      
  "DELETE",        "FROM",          "WHERE",         "UPDATE",      
  "SET",           "ALL",           "SELECT",        "LIMIT",       
  "ORDER",         "BY",            "HAVING",        "GROUP",       
  "USING",         "ON",            "OUTER",         "DOT",         
  "AS",            "DISTINCT",      "CASE",          "WHEN",        
  "THEN",          "ELSE",          "NULL",          "FLOAT",       
  "UNSPECVAL",     "LSBRACKET",     "RSBRACKET",     "SIMPLEPARAM", 
  "PNAME",         "PDESCR",        "PTYPE",         "PNULLOK",     
  "TEXTUAL",       "error",         "stmt",          "cmd",         
  "eos",           "compound",      "nm_opt",        "transtype",   
  "transilev",     "opt_comma",     "trans_opt_kw",  "ora_commit_write",
  "nm",            "opt_on_conflict",  "fullname",      "inscollist_opt",
  "rexprlist",     "ins_extra_values",  "inscollist",    "where_opt",   
  "expr",          "setlist",       "selectcmd",     "opt_compound_all",
  "distinct",      "selcollist",    "from",          "groupby_opt", 
  "having_opt",    "orderby_opt",   "limit_opt",     "sortlist",    
  "sortorder",     "rnexprlist",    "seltablist",    "stl_prefix",  
  "seltarget",     "on_cond",       "using_opt",     "jointype",    
  "as",            "sclp",          "starname",      "value",       
  "pvalue",        "uni_op",        "case_operand",  "case_exprlist",
  "case_else",     "paramspec",   
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
 /* 156 */ "expr ::= expr ILIKE expr",
 /* 157 */ "expr ::= expr NOTLIKE expr",
 /* 158 */ "expr ::= expr NOTILIKE expr",
 /* 159 */ "expr ::= expr REGEXP|REGEXP_CI|NOT_REGEXP|NOT_REGEXP_CI|SIMILAR expr",
 /* 160 */ "expr ::= expr BETWEEN expr AND expr",
 /* 161 */ "expr ::= expr NOT BETWEEN expr AND expr",
 /* 162 */ "expr ::= NOT expr",
 /* 163 */ "expr ::= BITNOT expr",
 /* 164 */ "expr ::= expr uni_op",
 /* 165 */ "expr ::= expr IS expr",
 /* 166 */ "expr ::= LP compound RP",
 /* 167 */ "expr ::= expr IN LP rexprlist RP",
 /* 168 */ "expr ::= expr IN LP compound RP",
 /* 169 */ "expr ::= expr NOT IN LP rexprlist RP",
 /* 170 */ "expr ::= expr NOT IN LP compound RP",
 /* 171 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 172 */ "case_operand ::= expr",
 /* 173 */ "case_operand ::=",
 /* 174 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 175 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 176 */ "case_else ::= ELSE expr",
 /* 177 */ "case_else ::=",
 /* 178 */ "uni_op ::= ISNULL",
 /* 179 */ "uni_op ::= IS NOTNULL",
 /* 180 */ "value ::= NULL",
 /* 181 */ "value ::= STRING",
 /* 182 */ "value ::= INTEGER",
 /* 183 */ "value ::= FLOAT",
 /* 184 */ "pvalue ::= UNSPECVAL LSBRACKET paramspec RSBRACKET",
 /* 185 */ "pvalue ::= value LSBRACKET paramspec RSBRACKET",
 /* 186 */ "pvalue ::= SIMPLEPARAM",
 /* 187 */ "paramspec ::=",
 /* 188 */ "paramspec ::= paramspec PNAME",
 /* 189 */ "paramspec ::= paramspec PDESCR",
 /* 190 */ "paramspec ::= paramspec PTYPE",
 /* 191 */ "paramspec ::= paramspec PNULLOK",
 /* 192 */ "nm ::= JOIN",
 /* 193 */ "nm ::= ID",
 /* 194 */ "nm ::= TEXTUAL",
 /* 195 */ "fullname ::= nm",
 /* 196 */ "fullname ::= nm DOT nm",
 /* 197 */ "fullname ::= nm DOT nm DOT nm",
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
** to gda_lemon_postgres_parser and gda_lemon_postgres_parserFree.
*/
void *gda_lemon_postgres_parserAlloc(void *(*mallocProc)(size_t)){
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
  gda_lemon_postgres_parserARG_FETCH;
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
    case 27: /* ILIKE */
    case 28: /* MATCH */
    case 29: /* PLAN */
    case 30: /* QUERY */
    case 31: /* KEY */
    case 32: /* OF */
    case 33: /* OFFSET */
    case 34: /* PRAGMA */
    case 35: /* RAISE */
    case 36: /* REPLACE */
    case 37: /* RESTRICT */
    case 38: /* ROW */
    case 39: /* TEMP */
    case 40: /* TRIGGER */
    case 41: /* VACUUM */
    case 42: /* VIEW */
    case 43: /* VIRTUAL */
    case 44: /* REINDEX */
    case 45: /* RENAME */
    case 46: /* CTIME_KW */
    case 47: /* IF */
    case 48: /* DELIMITER */
    case 49: /* COMMIT */
    case 50: /* ROLLBACK */
    case 51: /* ISOLATION */
    case 52: /* LEVEL */
    case 53: /* SERIALIZABLE */
    case 54: /* READ */
    case 55: /* COMMITTED */
    case 56: /* UNCOMMITTED */
    case 57: /* REPEATABLE */
    case 58: /* WRITE */
    case 59: /* ONLY */
    case 60: /* SAVEPOINT */
    case 61: /* RELEASE */
    case 62: /* COMMENT */
    case 63: /* FORCE */
    case 64: /* WAIT */
    case 65: /* NOWAIT */
    case 66: /* BATCH */
    case 67: /* OR */
    case 68: /* AND */
    case 69: /* NOT */
    case 70: /* IS */
    case 71: /* NOTLIKE */
    case 72: /* NOTILIKE */
    case 73: /* IN */
    case 74: /* ISNULL */
    case 75: /* NOTNULL */
    case 76: /* DIFF */
    case 77: /* EQ */
    case 78: /* BETWEEN */
    case 79: /* GT */
    case 80: /* LEQ */
    case 81: /* LT */
    case 82: /* GEQ */
    case 83: /* REGEXP */
    case 84: /* REGEXP_CI */
    case 85: /* NOT_REGEXP */
    case 86: /* NOT_REGEXP_CI */
    case 87: /* SIMILAR */
    case 88: /* ESCAPE */
    case 89: /* BITAND */
    case 90: /* BITOR */
    case 91: /* LSHIFT */
    case 92: /* RSHIFT */
    case 93: /* PLUS */
    case 94: /* MINUS */
    case 95: /* STAR */
    case 96: /* SLASH */
    case 97: /* REM */
    case 98: /* CONCAT */
    case 99: /* COLLATE */
    case 100: /* UMINUS */
    case 101: /* UPLUS */
    case 102: /* BITNOT */
    case 103: /* LP */
    case 104: /* RP */
    case 105: /* JOIN */
    case 106: /* INNER */
    case 107: /* NATURAL */
    case 108: /* LEFT */
    case 109: /* RIGHT */
    case 110: /* FULL */
    case 111: /* CROSS */
    case 112: /* UNION */
    case 113: /* EXCEPT */
    case 114: /* INTERSECT */
    case 115: /* PGCAST */
    case 116: /* ILLEGAL */
    case 117: /* SQLCOMMENT */
    case 118: /* SEMI */
    case 119: /* END_OF_FILE */
    case 120: /* TRANSACTION */
    case 121: /* STRING */
    case 122: /* COMMA */
    case 123: /* INTEGER */
    case 124: /* TO */
    case 125: /* INSERT */
    case 126: /* INTO */
    case 127: /* VALUES */
    case 128: /* DELETE */
    case 129: /* FROM */
    case 130: /* WHERE */
    case 131: /* UPDATE */
    case 132: /* SET */
    case 133: /* ALL */
    case 134: /* SELECT */
    case 135: /* LIMIT */
    case 136: /* ORDER */
    case 137: /* BY */
    case 138: /* HAVING */
    case 139: /* GROUP */
    case 140: /* USING */
    case 141: /* ON */
    case 142: /* OUTER */
    case 143: /* DOT */
    case 144: /* AS */
    case 145: /* DISTINCT */
    case 146: /* CASE */
    case 147: /* WHEN */
    case 148: /* THEN */
    case 149: /* ELSE */
    case 150: /* NULL */
    case 151: /* FLOAT */
    case 152: /* UNSPECVAL */
    case 153: /* LSBRACKET */
    case 154: /* RSBRACKET */
    case 155: /* SIMPLEPARAM */
    case 156: /* PNAME */
    case 157: /* PDESCR */
    case 158: /* PTYPE */
    case 159: /* PNULLOK */
    case 160: /* TEXTUAL */
{
#line 9 "./parser.y"
if ((yypminor->yy0)) {
#ifdef GDA_DEBUG_NO
		 gchar *str = gda_sql_value_stringify ((yypminor->yy0));
		 g_print ("___ token destructor /%s/\n", str)
		 g_free (str);
#endif
		 g_value_unset ((yypminor->yy0)); g_free ((yypminor->yy0));}
#line 1407 "parser.c"
}
      break;
    case 162: /* stmt */
{
#line 280 "./parser.y"
g_print ("Statement destroyed by parser: %p\n", (yypminor->yy116)); gda_sql_statement_free ((yypminor->yy116));
#line 1414 "parser.c"
}
      break;
    case 163: /* cmd */
    case 165: /* compound */
    case 182: /* selectcmd */
{
#line 303 "./parser.y"
gda_sql_statement_free ((yypminor->yy116));
#line 1423 "parser.c"
}
      break;
    case 175: /* inscollist_opt */
    case 178: /* inscollist */
    case 198: /* using_opt */
{
#line 480 "./parser.y"
if ((yypminor->yy333)) {g_slist_foreach ((yypminor->yy333), (GFunc) gda_sql_field_free, NULL); g_slist_free ((yypminor->yy333));}
#line 1432 "parser.c"
}
      break;
    case 176: /* rexprlist */
    case 193: /* rnexprlist */
{
#line 766 "./parser.y"
if ((yypminor->yy325)) {g_slist_foreach ((yypminor->yy325), (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy325));}
#line 1440 "parser.c"
}
      break;
    case 177: /* ins_extra_values */
{
#line 469 "./parser.y"
GSList *list;
		for (list = (yypminor->yy333); list; list = list->next) {
			g_slist_foreach ((GSList*) list->data, (GFunc) gda_sql_field_free, NULL); 
			g_slist_free ((GSList*) list->data);
		}
		g_slist_free ((yypminor->yy333));

#line 1453 "parser.c"
}
      break;
    case 179: /* where_opt */
    case 180: /* expr */
    case 188: /* having_opt */
    case 197: /* on_cond */
    case 204: /* pvalue */
{
#line 502 "./parser.y"
gda_sql_expr_free ((yypminor->yy70));
#line 1464 "parser.c"
}
      break;
    case 181: /* setlist */
{
#line 522 "./parser.y"
GSList *list;
	for (list = (yypminor->yy333); list; list = list->next) {
		UpdateSet *set = (UpdateSet*) list->data;
		g_value_reset (set->fname); g_free (set->fname);
		gda_sql_expr_free (set->expr);
		g_free (set);
	}
	g_slist_free ((yypminor->yy333));

#line 1479 "parser.c"
}
      break;
    case 184: /* distinct */
{
#line 752 "./parser.y"
if ((yypminor->yy297)) {if ((yypminor->yy297)->expr) gda_sql_expr_free ((yypminor->yy297)->expr); g_free ((yypminor->yy297));}
#line 1486 "parser.c"
}
      break;
    case 185: /* selcollist */
    case 201: /* sclp */
{
#line 709 "./parser.y"
g_slist_foreach ((yypminor->yy325), (GFunc) gda_sql_select_field_free, NULL); g_slist_free ((yypminor->yy325));
#line 1494 "parser.c"
}
      break;
    case 186: /* from */
    case 194: /* seltablist */
    case 195: /* stl_prefix */
{
#line 635 "./parser.y"
gda_sql_select_from_free ((yypminor->yy191));
#line 1503 "parser.c"
}
      break;
    case 187: /* groupby_opt */
{
#line 630 "./parser.y"
if ((yypminor->yy333)) {g_slist_foreach ((yypminor->yy333), (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy333));}
#line 1510 "parser.c"
}
      break;
    case 189: /* orderby_opt */
    case 191: /* sortlist */
{
#line 599 "./parser.y"
if ((yypminor->yy325)) {g_slist_foreach ((yypminor->yy325), (GFunc) gda_sql_select_order_free, NULL); g_slist_free ((yypminor->yy325));}
#line 1518 "parser.c"
}
      break;
    case 190: /* limit_opt */
{
#line 592 "./parser.y"
gda_sql_expr_free ((yypminor->yy44).count); gda_sql_expr_free ((yypminor->yy44).offset);
#line 1525 "parser.c"
}
      break;
    case 196: /* seltarget */
{
#line 694 "./parser.y"
gda_sql_select_target_free ((yypminor->yy134));
#line 1532 "parser.c"
}
      break;
    case 206: /* case_operand */
    case 208: /* case_else */
{
#line 933 "./parser.y"
gda_sql_expr_free ((yypminor->yy146));
#line 1540 "parser.c"
}
      break;
    case 207: /* case_exprlist */
{
#line 938 "./parser.y"
g_slist_foreach ((yypminor->yy59).when_list, (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy59).when_list);
	g_slist_foreach ((yypminor->yy59).then_list, (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy59).then_list);
#line 1548 "parser.c"
}
      break;
    case 209: /* paramspec */
{
#line 976 "./parser.y"
gda_sql_param_spec_free ((yypminor->yy339));
#line 1555 "parser.c"
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
**       obtained from gda_lemon_postgres_parserAlloc.
** <li>  A pointer to a function used to reclaim memory obtained
**       from malloc.
** </ul>
*/
void gda_lemon_postgres_parserFree(
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
int gda_lemon_postgres_parserStackPeak(void *p){
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
   gda_lemon_postgres_parserARG_FETCH;
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
#line 1741 "parser.c"
   gda_lemon_postgres_parserARG_STORE; /* Suppress warning about unused %extra_argument var */
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
  { 162, 2 },
  { 162, 2 },
  { 163, 3 },
  { 165, 3 },
  { 164, 1 },
  { 164, 1 },
  { 163, 1 },
  { 163, 3 },
  { 163, 4 },
  { 163, 3 },
  { 163, 2 },
  { 163, 3 },
  { 163, 3 },
  { 163, 5 },
  { 163, 5 },
  { 163, 4 },
  { 163, 4 },
  { 163, 3 },
  { 163, 2 },
  { 163, 3 },
  { 163, 3 },
  { 163, 5 },
  { 163, 3 },
  { 163, 4 },
  { 163, 2 },
  { 163, 3 },
  { 171, 2 },
  { 171, 2 },
  { 171, 2 },
  { 171, 2 },
  { 171, 3 },
  { 171, 3 },
  { 171, 3 },
  { 171, 3 },
  { 170, 0 },
  { 170, 1 },
  { 169, 0 },
  { 169, 1 },
  { 168, 3 },
  { 168, 4 },
  { 168, 4 },
  { 168, 4 },
  { 166, 0 },
  { 166, 1 },
  { 167, 1 },
  { 167, 1 },
  { 167, 1 },
  { 167, 2 },
  { 167, 2 },
  { 163, 2 },
  { 163, 3 },
  { 163, 2 },
  { 163, 4 },
  { 163, 5 },
  { 163, 9 },
  { 163, 10 },
  { 163, 6 },
  { 173, 0 },
  { 173, 2 },
  { 177, 5 },
  { 177, 4 },
  { 175, 0 },
  { 175, 3 },
  { 178, 3 },
  { 178, 1 },
  { 163, 4 },
  { 179, 0 },
  { 179, 2 },
  { 163, 6 },
  { 181, 5 },
  { 181, 3 },
  { 165, 1 },
  { 165, 4 },
  { 165, 4 },
  { 165, 4 },
  { 183, 0 },
  { 183, 1 },
  { 182, 9 },
  { 190, 0 },
  { 190, 2 },
  { 190, 4 },
  { 190, 4 },
  { 189, 0 },
  { 189, 3 },
  { 191, 4 },
  { 191, 2 },
  { 192, 1 },
  { 192, 1 },
  { 192, 0 },
  { 188, 0 },
  { 188, 2 },
  { 187, 0 },
  { 187, 3 },
  { 186, 0 },
  { 186, 2 },
  { 194, 4 },
  { 198, 4 },
  { 198, 0 },
  { 195, 0 },
  { 195, 2 },
  { 197, 2 },
  { 197, 0 },
  { 199, 1 },
  { 199, 1 },
  { 199, 2 },
  { 199, 2 },
  { 199, 2 },
  { 199, 2 },
  { 199, 3 },
  { 199, 2 },
  { 199, 3 },
  { 199, 2 },
  { 199, 3 },
  { 196, 2 },
  { 196, 2 },
  { 196, 4 },
  { 201, 2 },
  { 201, 0 },
  { 185, 3 },
  { 185, 2 },
  { 202, 1 },
  { 202, 3 },
  { 202, 5 },
  { 200, 2 },
  { 200, 2 },
  { 200, 0 },
  { 184, 0 },
  { 184, 1 },
  { 184, 1 },
  { 184, 3 },
  { 193, 3 },
  { 193, 1 },
  { 176, 0 },
  { 176, 3 },
  { 176, 1 },
  { 180, 1 },
  { 180, 1 },
  { 180, 3 },
  { 180, 1 },
  { 180, 4 },
  { 180, 4 },
  { 180, 4 },
  { 180, 6 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 2 },
  { 180, 2 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 3 },
  { 180, 5 },
  { 180, 6 },
  { 180, 2 },
  { 180, 2 },
  { 180, 2 },
  { 180, 3 },
  { 180, 3 },
  { 180, 5 },
  { 180, 5 },
  { 180, 6 },
  { 180, 6 },
  { 180, 5 },
  { 206, 1 },
  { 206, 0 },
  { 207, 5 },
  { 207, 4 },
  { 208, 2 },
  { 208, 0 },
  { 205, 1 },
  { 205, 2 },
  { 203, 1 },
  { 203, 1 },
  { 203, 1 },
  { 203, 1 },
  { 204, 4 },
  { 204, 4 },
  { 204, 1 },
  { 209, 0 },
  { 209, 2 },
  { 209, 2 },
  { 209, 2 },
  { 209, 2 },
  { 172, 1 },
  { 172, 1 },
  { 172, 1 },
  { 174, 1 },
  { 174, 3 },
  { 174, 5 },
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
  gda_lemon_postgres_parserARG_FETCH;
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
#line 281 "./parser.y"
{pdata->parsed_statement = yymsp[-1].minor.yy116;}
#line 2053 "parser.c"
        break;
      case 1: /* stmt ::= compound eos */
#line 282 "./parser.y"
{
	GdaSqlStatementCompound *scompound = (GdaSqlStatementCompound *) yymsp[-1].minor.yy116->contents;
	if (scompound->stmt_list->next)
		/* real compound (multiple statements) */
		pdata->parsed_statement = yymsp[-1].minor.yy116;
	else {
		/* false compound (only 1 select) */
		pdata->parsed_statement = (GdaSqlStatement*) scompound->stmt_list->data;
		GDA_SQL_ANY_PART (pdata->parsed_statement->contents)->parent = NULL;
		g_slist_free (scompound->stmt_list);
		scompound->stmt_list = NULL;
		gda_sql_statement_free (yymsp[-1].minor.yy116);
	}
}
#line 2071 "parser.c"
        break;
      case 2: /* cmd ::= LP cmd RP */
      case 3: /* compound ::= LP compound RP */ yytestcase(yyruleno==3);
#line 296 "./parser.y"
{yygotominor.yy116 = yymsp[-1].minor.yy116;  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 2079 "parser.c"
        break;
      case 4: /* eos ::= SEMI */
#line 299 "./parser.y"
{
  yy_destructor(yypParser,118,&yymsp[0].minor);
}
#line 2086 "parser.c"
        break;
      case 5: /* eos ::= END_OF_FILE */
#line 300 "./parser.y"
{
  yy_destructor(yypParser,119,&yymsp[0].minor);
}
#line 2093 "parser.c"
        break;
      case 6: /* cmd ::= BEGIN */
#line 308 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);  yy_destructor(yypParser,8,&yymsp[0].minor);
}
#line 2099 "parser.c"
        break;
      case 7: /* cmd ::= BEGIN TRANSACTION nm_opt */
#line 309 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					 gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2108 "parser.c"
        break;
      case 8: /* cmd ::= BEGIN transtype TRANSACTION nm_opt */
#line 313 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						      gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-2].minor.yy0);
						      gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2118 "parser.c"
        break;
      case 9: /* cmd ::= BEGIN transtype nm_opt */
#line 318 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					  gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-1].minor.yy0);
					  gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
}
#line 2127 "parser.c"
        break;
      case 10: /* cmd ::= BEGIN transilev */
#line 323 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
				gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-1].minor);
}
#line 2135 "parser.c"
        break;
      case 11: /* cmd ::= BEGIN TRANSACTION transilev */
#line 327 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					    gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2144 "parser.c"
        break;
      case 12: /* cmd ::= BEGIN TRANSACTION transtype */
#line 331 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					    gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2153 "parser.c"
        break;
      case 13: /* cmd ::= BEGIN TRANSACTION transtype opt_comma transilev */
#line 335 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
								   gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-2].minor.yy0);
								   gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-4].minor);
  yy_destructor(yypParser,120,&yymsp[-3].minor);
}
#line 2163 "parser.c"
        break;
      case 14: /* cmd ::= BEGIN TRANSACTION transilev opt_comma transtype */
#line 340 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
								   gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[0].minor.yy0);
								   gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[-2].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-4].minor);
  yy_destructor(yypParser,120,&yymsp[-3].minor);
}
#line 2173 "parser.c"
        break;
      case 15: /* cmd ::= BEGIN transtype opt_comma transilev */
#line 345 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						       gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-2].minor.yy0);
						       gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
}
#line 2182 "parser.c"
        break;
      case 16: /* cmd ::= BEGIN transilev opt_comma transtype */
#line 350 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						       gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[0].minor.yy0);
						       gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[-2].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
}
#line 2191 "parser.c"
        break;
      case 17: /* cmd ::= END trans_opt_kw nm_opt */
#line 355 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
					gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,17,&yymsp[-2].minor);
}
#line 2199 "parser.c"
        break;
      case 18: /* cmd ::= COMMIT nm_opt */
#line 359 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
			      gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-1].minor);
}
#line 2207 "parser.c"
        break;
      case 19: /* cmd ::= COMMIT TRANSACTION nm_opt */
#line 363 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
					  gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-2].minor);
  yy_destructor(yypParser,120,&yymsp[-1].minor);
}
#line 2216 "parser.c"
        break;
      case 20: /* cmd ::= COMMIT FORCE STRING */
#line 367 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-2].minor);
  yy_destructor(yypParser,63,&yymsp[-1].minor);
  yy_destructor(yypParser,121,&yymsp[0].minor);
}
#line 2224 "parser.c"
        break;
      case 21: /* cmd ::= COMMIT FORCE STRING COMMA INTEGER */
#line 368 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-4].minor);
  yy_destructor(yypParser,63,&yymsp[-3].minor);
  yy_destructor(yypParser,121,&yymsp[-2].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
  yy_destructor(yypParser,123,&yymsp[0].minor);
}
#line 2234 "parser.c"
        break;
      case 22: /* cmd ::= COMMIT COMMENT STRING */
#line 369 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-2].minor);
  yy_destructor(yypParser,62,&yymsp[-1].minor);
  yy_destructor(yypParser,121,&yymsp[0].minor);
}
#line 2242 "parser.c"
        break;
      case 23: /* cmd ::= COMMIT COMMENT STRING ora_commit_write */
#line 370 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-3].minor);
  yy_destructor(yypParser,62,&yymsp[-2].minor);
  yy_destructor(yypParser,121,&yymsp[-1].minor);
}
#line 2250 "parser.c"
        break;
      case 24: /* cmd ::= COMMIT ora_commit_write */
#line 371 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-1].minor);
}
#line 2256 "parser.c"
        break;
      case 25: /* cmd ::= ROLLBACK trans_opt_kw nm_opt */
#line 373 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK);
					     gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,50,&yymsp[-2].minor);
}
#line 2264 "parser.c"
        break;
      case 26: /* ora_commit_write ::= WRITE IMMEDIATE */
#line 377 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,23,&yymsp[0].minor);
}
#line 2272 "parser.c"
        break;
      case 27: /* ora_commit_write ::= WRITE BATCH */
#line 378 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,66,&yymsp[0].minor);
}
#line 2280 "parser.c"
        break;
      case 28: /* ora_commit_write ::= WRITE WAIT */
#line 379 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2288 "parser.c"
        break;
      case 29: /* ora_commit_write ::= WRITE NOWAIT */
#line 380 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2296 "parser.c"
        break;
      case 30: /* ora_commit_write ::= WRITE IMMEDIATE WAIT */
#line 381 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,23,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2305 "parser.c"
        break;
      case 31: /* ora_commit_write ::= WRITE IMMEDIATE NOWAIT */
#line 382 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,23,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2314 "parser.c"
        break;
      case 32: /* ora_commit_write ::= WRITE BATCH WAIT */
#line 383 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,66,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2323 "parser.c"
        break;
      case 33: /* ora_commit_write ::= WRITE BATCH NOWAIT */
#line 384 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,66,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2332 "parser.c"
        break;
      case 35: /* trans_opt_kw ::= TRANSACTION */
#line 387 "./parser.y"
{
  yy_destructor(yypParser,120,&yymsp[0].minor);
}
#line 2339 "parser.c"
        break;
      case 37: /* opt_comma ::= COMMA */
#line 390 "./parser.y"
{
  yy_destructor(yypParser,122,&yymsp[0].minor);
}
#line 2346 "parser.c"
        break;
      case 38: /* transilev ::= ISOLATION LEVEL SERIALIZABLE */
#line 393 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_SERIALIZABLE;  yy_destructor(yypParser,51,&yymsp[-2].minor);
  yy_destructor(yypParser,52,&yymsp[-1].minor);
  yy_destructor(yypParser,53,&yymsp[0].minor);
}
#line 2354 "parser.c"
        break;
      case 39: /* transilev ::= ISOLATION LEVEL REPEATABLE READ */
#line 394 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_REPEATABLE_READ;  yy_destructor(yypParser,51,&yymsp[-3].minor);
  yy_destructor(yypParser,52,&yymsp[-2].minor);
  yy_destructor(yypParser,57,&yymsp[-1].minor);
  yy_destructor(yypParser,54,&yymsp[0].minor);
}
#line 2363 "parser.c"
        break;
      case 40: /* transilev ::= ISOLATION LEVEL READ COMMITTED */
#line 395 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_READ_COMMITTED;  yy_destructor(yypParser,51,&yymsp[-3].minor);
  yy_destructor(yypParser,52,&yymsp[-2].minor);
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,55,&yymsp[0].minor);
}
#line 2372 "parser.c"
        break;
      case 41: /* transilev ::= ISOLATION LEVEL READ UNCOMMITTED */
#line 396 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_READ_UNCOMMITTED;  yy_destructor(yypParser,51,&yymsp[-3].minor);
  yy_destructor(yypParser,52,&yymsp[-2].minor);
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,56,&yymsp[0].minor);
}
#line 2381 "parser.c"
        break;
      case 42: /* nm_opt ::= */
      case 57: /* opt_on_conflict ::= */ yytestcase(yyruleno==57);
      case 125: /* as ::= */ yytestcase(yyruleno==125);
#line 398 "./parser.y"
{yygotominor.yy0 = NULL;}
#line 2388 "parser.c"
        break;
      case 43: /* nm_opt ::= nm */
      case 44: /* transtype ::= DEFERRED */ yytestcase(yyruleno==44);
      case 45: /* transtype ::= IMMEDIATE */ yytestcase(yyruleno==45);
      case 46: /* transtype ::= EXCLUSIVE */ yytestcase(yyruleno==46);
      case 120: /* starname ::= STAR */ yytestcase(yyruleno==120);
      case 181: /* value ::= STRING */ yytestcase(yyruleno==181);
      case 182: /* value ::= INTEGER */ yytestcase(yyruleno==182);
      case 183: /* value ::= FLOAT */ yytestcase(yyruleno==183);
      case 192: /* nm ::= JOIN */ yytestcase(yyruleno==192);
      case 193: /* nm ::= ID */ yytestcase(yyruleno==193);
      case 194: /* nm ::= TEXTUAL */ yytestcase(yyruleno==194);
      case 195: /* fullname ::= nm */ yytestcase(yyruleno==195);
#line 399 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;}
#line 2404 "parser.c"
        break;
      case 47: /* transtype ::= READ WRITE */
#line 404 "./parser.y"
{yygotominor.yy0 = g_new0 (GValue, 1);
			      g_value_init (yygotominor.yy0, G_TYPE_STRING);
			      g_value_set_string (yygotominor.yy0, "READ_WRITE");
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,58,&yymsp[0].minor);
}
#line 2414 "parser.c"
        break;
      case 48: /* transtype ::= READ ONLY */
#line 408 "./parser.y"
{yygotominor.yy0 = g_new0 (GValue, 1);
			     g_value_init (yygotominor.yy0, G_TYPE_STRING);
			     g_value_set_string (yygotominor.yy0, "READ_ONLY");
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,59,&yymsp[0].minor);
}
#line 2424 "parser.c"
        break;
      case 49: /* cmd ::= SAVEPOINT nm */
#line 416 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_SAVEPOINT);
				    gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2432 "parser.c"
        break;
      case 50: /* cmd ::= RELEASE SAVEPOINT nm */
#line 420 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE_SAVEPOINT);
				     gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,61,&yymsp[-2].minor);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2441 "parser.c"
        break;
      case 51: /* cmd ::= RELEASE nm */
#line 424 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE_SAVEPOINT);
			   gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,61,&yymsp[-1].minor);
}
#line 2449 "parser.c"
        break;
      case 52: /* cmd ::= ROLLBACK trans_opt_kw TO nm */
#line 428 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT);
					    gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,50,&yymsp[-3].minor);
  yy_destructor(yypParser,124,&yymsp[-1].minor);
}
#line 2458 "parser.c"
        break;
      case 53: /* cmd ::= ROLLBACK trans_opt_kw TO SAVEPOINT nm */
#line 432 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT);
						      gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,50,&yymsp[-4].minor);
  yy_destructor(yypParser,124,&yymsp[-2].minor);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2468 "parser.c"
        break;
      case 54: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP */
#line 439 "./parser.y"
{
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
	gda_sql_statement_insert_take_table_name (yygotominor.yy116, yymsp[-5].minor.yy0);
	gda_sql_statement_insert_take_fields_list (yygotominor.yy116, yymsp[-4].minor.yy333);
	gda_sql_statement_insert_take_1_values_list (yygotominor.yy116, g_slist_reverse (yymsp[-1].minor.yy325));
	gda_sql_statement_insert_take_on_conflict (yygotominor.yy116, yymsp[-7].minor.yy0);
  yy_destructor(yypParser,125,&yymsp[-8].minor);
  yy_destructor(yypParser,126,&yymsp[-6].minor);
  yy_destructor(yypParser,127,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 2484 "parser.c"
        break;
      case 55: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP ins_extra_values */
#line 447 "./parser.y"
{
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
	gda_sql_statement_insert_take_table_name (yygotominor.yy116, yymsp[-6].minor.yy0);
	gda_sql_statement_insert_take_fields_list (yygotominor.yy116, yymsp[-5].minor.yy333);
	gda_sql_statement_insert_take_1_values_list (yygotominor.yy116, g_slist_reverse (yymsp[-2].minor.yy325));
	gda_sql_statement_insert_take_extra_values_list (yygotominor.yy116, yymsp[0].minor.yy333);
	gda_sql_statement_insert_take_on_conflict (yygotominor.yy116, yymsp[-8].minor.yy0);
  yy_destructor(yypParser,125,&yymsp[-9].minor);
  yy_destructor(yypParser,126,&yymsp[-7].minor);
  yy_destructor(yypParser,127,&yymsp[-4].minor);
  yy_destructor(yypParser,103,&yymsp[-3].minor);
  yy_destructor(yypParser,104,&yymsp[-1].minor);
}
#line 2501 "parser.c"
        break;
      case 56: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt compound */
#line 456 "./parser.y"
{
        yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
        gda_sql_statement_insert_take_table_name (yygotominor.yy116, yymsp[-2].minor.yy0);
        gda_sql_statement_insert_take_fields_list (yygotominor.yy116, yymsp[-1].minor.yy333);
        gda_sql_statement_insert_take_select (yygotominor.yy116, yymsp[0].minor.yy116);
        gda_sql_statement_insert_take_on_conflict (yygotominor.yy116, yymsp[-4].minor.yy0);
  yy_destructor(yypParser,125,&yymsp[-5].minor);
  yy_destructor(yypParser,126,&yymsp[-3].minor);
}
#line 2514 "parser.c"
        break;
      case 58: /* opt_on_conflict ::= OR ID */
#line 466 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;  yy_destructor(yypParser,67,&yymsp[-1].minor);
}
#line 2520 "parser.c"
        break;
      case 59: /* ins_extra_values ::= ins_extra_values COMMA LP rexprlist RP */
#line 476 "./parser.y"
{yygotominor.yy333 = g_slist_append (yymsp[-4].minor.yy333, g_slist_reverse (yymsp[-1].minor.yy325));  yy_destructor(yypParser,122,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 2528 "parser.c"
        break;
      case 60: /* ins_extra_values ::= COMMA LP rexprlist RP */
#line 477 "./parser.y"
{yygotominor.yy333 = g_slist_append (NULL, g_slist_reverse (yymsp[-1].minor.yy325));  yy_destructor(yypParser,122,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 2536 "parser.c"
        break;
      case 61: /* inscollist_opt ::= */
      case 97: /* using_opt ::= */ yytestcase(yyruleno==97);
#line 481 "./parser.y"
{yygotominor.yy333 = NULL;}
#line 2542 "parser.c"
        break;
      case 62: /* inscollist_opt ::= LP inscollist RP */
#line 482 "./parser.y"
{yygotominor.yy333 = yymsp[-1].minor.yy333;  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 2549 "parser.c"
        break;
      case 63: /* inscollist ::= inscollist COMMA fullname */
#line 486 "./parser.y"
{GdaSqlField *field;
						    field = gda_sql_field_new (NULL);
						    gda_sql_field_take_name (field, yymsp[0].minor.yy0);
						    yygotominor.yy333 = g_slist_append (yymsp[-2].minor.yy333, field);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2559 "parser.c"
        break;
      case 64: /* inscollist ::= fullname */
#line 491 "./parser.y"
{GdaSqlField *field = gda_sql_field_new (NULL);
				gda_sql_field_take_name (field, yymsp[0].minor.yy0);
				yygotominor.yy333 = g_slist_prepend (NULL, field);
}
#line 2567 "parser.c"
        break;
      case 65: /* cmd ::= DELETE FROM fullname where_opt */
#line 497 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE); 
						  gda_sql_statement_delete_take_table_name (yygotominor.yy116, yymsp[-1].minor.yy0);
						  gda_sql_statement_delete_take_condition (yygotominor.yy116, yymsp[0].minor.yy70);  yy_destructor(yypParser,128,&yymsp[-3].minor);
  yy_destructor(yypParser,129,&yymsp[-2].minor);
}
#line 2576 "parser.c"
        break;
      case 66: /* where_opt ::= */
      case 89: /* having_opt ::= */ yytestcase(yyruleno==89);
      case 101: /* on_cond ::= */ yytestcase(yyruleno==101);
#line 503 "./parser.y"
{yygotominor.yy70 = NULL;}
#line 2583 "parser.c"
        break;
      case 67: /* where_opt ::= WHERE expr */
#line 504 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;  yy_destructor(yypParser,130,&yymsp[-1].minor);
}
#line 2589 "parser.c"
        break;
      case 68: /* cmd ::= UPDATE opt_on_conflict fullname SET setlist where_opt */
#line 507 "./parser.y"
{
	GSList *list;
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_UPDATE);
	gda_sql_statement_update_take_table_name (yygotominor.yy116, yymsp[-3].minor.yy0);
	gda_sql_statement_update_take_on_conflict (yygotominor.yy116, yymsp[-4].minor.yy0);
	gda_sql_statement_update_take_condition (yygotominor.yy116, yymsp[0].minor.yy70);
	for (list = yymsp[-1].minor.yy333; list; list = list->next) {
		UpdateSet *set = (UpdateSet*) list->data;
		gda_sql_statement_update_take_set_value (yygotominor.yy116, set->fname, set->expr);
		g_free (set);
	}
	g_slist_free (yymsp[-1].minor.yy333);
  yy_destructor(yypParser,131,&yymsp[-5].minor);
  yy_destructor(yypParser,132,&yymsp[-2].minor);
}
#line 2608 "parser.c"
        break;
      case 69: /* setlist ::= setlist COMMA fullname EQ expr */
#line 531 "./parser.y"
{UpdateSet *set;
							 set = g_new (UpdateSet, 1);
							 set->fname = yymsp[-2].minor.yy0;
							 set->expr = yymsp[0].minor.yy70;
							 yygotominor.yy333 = g_slist_append (yymsp[-4].minor.yy333, set);
  yy_destructor(yypParser,122,&yymsp[-3].minor);
  yy_destructor(yypParser,77,&yymsp[-1].minor);
}
#line 2620 "parser.c"
        break;
      case 70: /* setlist ::= fullname EQ expr */
#line 537 "./parser.y"
{UpdateSet *set;
					set = g_new (UpdateSet, 1);
					set->fname = yymsp[-2].minor.yy0;
					set->expr = yymsp[0].minor.yy70;
					yygotominor.yy333 = g_slist_append (NULL, set);
  yy_destructor(yypParser,77,&yymsp[-1].minor);
}
#line 2631 "parser.c"
        break;
      case 71: /* compound ::= selectcmd */
#line 548 "./parser.y"
{
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMPOUND);
	gda_sql_statement_compound_take_stmt (yygotominor.yy116, yymsp[0].minor.yy116);
}
#line 2639 "parser.c"
        break;
      case 72: /* compound ::= compound UNION opt_compound_all compound */
#line 552 "./parser.y"
{
	yygotominor.yy116 = compose_multiple_compounds (yymsp[-1].minor.yy276 ? GDA_SQL_STATEMENT_COMPOUND_UNION_ALL : GDA_SQL_STATEMENT_COMPOUND_UNION,
					yymsp[-3].minor.yy116, yymsp[0].minor.yy116);
  yy_destructor(yypParser,112,&yymsp[-2].minor);
}
#line 2648 "parser.c"
        break;
      case 73: /* compound ::= compound EXCEPT opt_compound_all compound */
#line 557 "./parser.y"
{
	yygotominor.yy116 = compose_multiple_compounds (yymsp[-1].minor.yy276 ? GDA_SQL_STATEMENT_COMPOUND_EXCEPT_ALL : GDA_SQL_STATEMENT_COMPOUND_EXCEPT,
					yymsp[-3].minor.yy116, yymsp[0].minor.yy116);
  yy_destructor(yypParser,113,&yymsp[-2].minor);
}
#line 2657 "parser.c"
        break;
      case 74: /* compound ::= compound INTERSECT opt_compound_all compound */
#line 562 "./parser.y"
{
	yygotominor.yy116 = compose_multiple_compounds (yymsp[-1].minor.yy276 ? GDA_SQL_STATEMENT_COMPOUND_INTERSECT_ALL : GDA_SQL_STATEMENT_COMPOUND_INTERSECT,
					yymsp[-3].minor.yy116, yymsp[0].minor.yy116);
  yy_destructor(yypParser,114,&yymsp[-2].minor);
}
#line 2666 "parser.c"
        break;
      case 75: /* opt_compound_all ::= */
#line 568 "./parser.y"
{yygotominor.yy276 = FALSE;}
#line 2671 "parser.c"
        break;
      case 76: /* opt_compound_all ::= ALL */
#line 569 "./parser.y"
{yygotominor.yy276 = TRUE;  yy_destructor(yypParser,133,&yymsp[0].minor);
}
#line 2677 "parser.c"
        break;
      case 77: /* selectcmd ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 576 "./parser.y"
{
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_SELECT);
	if (yymsp[-7].minor.yy297) {
		gda_sql_statement_select_take_distinct (yygotominor.yy116, yymsp[-7].minor.yy297->distinct, yymsp[-7].minor.yy297->expr);
		g_free (yymsp[-7].minor.yy297);
	}
	gda_sql_statement_select_take_expr_list (yygotominor.yy116, yymsp[-6].minor.yy325);
	gda_sql_statement_select_take_from (yygotominor.yy116, yymsp[-5].minor.yy191);
	gda_sql_statement_select_take_where_cond (yygotominor.yy116, yymsp[-4].minor.yy70);
	gda_sql_statement_select_take_group_by (yygotominor.yy116, yymsp[-3].minor.yy333);
	gda_sql_statement_select_take_having_cond (yygotominor.yy116, yymsp[-2].minor.yy70);
	gda_sql_statement_select_take_order_by (yygotominor.yy116, yymsp[-1].minor.yy325);
	gda_sql_statement_select_take_limits (yygotominor.yy116, yymsp[0].minor.yy44.count, yymsp[0].minor.yy44.offset);
  yy_destructor(yypParser,134,&yymsp[-8].minor);
}
#line 2696 "parser.c"
        break;
      case 78: /* limit_opt ::= */
#line 593 "./parser.y"
{yygotominor.yy44.count = NULL; yygotominor.yy44.offset = NULL;}
#line 2701 "parser.c"
        break;
      case 79: /* limit_opt ::= LIMIT expr */
#line 594 "./parser.y"
{yygotominor.yy44.count = yymsp[0].minor.yy70; yygotominor.yy44.offset = NULL;  yy_destructor(yypParser,135,&yymsp[-1].minor);
}
#line 2707 "parser.c"
        break;
      case 80: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 595 "./parser.y"
{yygotominor.yy44.count = yymsp[-2].minor.yy70; yygotominor.yy44.offset = yymsp[0].minor.yy70;  yy_destructor(yypParser,135,&yymsp[-3].minor);
  yy_destructor(yypParser,33,&yymsp[-1].minor);
}
#line 2714 "parser.c"
        break;
      case 81: /* limit_opt ::= LIMIT expr COMMA expr */
#line 596 "./parser.y"
{yygotominor.yy44.count = yymsp[-2].minor.yy70; yygotominor.yy44.offset = yymsp[0].minor.yy70;  yy_destructor(yypParser,135,&yymsp[-3].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2721 "parser.c"
        break;
      case 82: /* orderby_opt ::= */
#line 600 "./parser.y"
{yygotominor.yy325 = 0;}
#line 2726 "parser.c"
        break;
      case 83: /* orderby_opt ::= ORDER BY sortlist */
#line 601 "./parser.y"
{yygotominor.yy325 = yymsp[0].minor.yy325;  yy_destructor(yypParser,136,&yymsp[-2].minor);
  yy_destructor(yypParser,137,&yymsp[-1].minor);
}
#line 2733 "parser.c"
        break;
      case 84: /* sortlist ::= sortlist COMMA expr sortorder */
#line 605 "./parser.y"
{GdaSqlSelectOrder *order;
							 order = gda_sql_select_order_new (NULL);
							 order->expr = yymsp[-1].minor.yy70;
							 order->asc = yymsp[0].minor.yy276;
							 yygotominor.yy325 = g_slist_append (yymsp[-3].minor.yy325, order);
  yy_destructor(yypParser,122,&yymsp[-2].minor);
}
#line 2744 "parser.c"
        break;
      case 85: /* sortlist ::= expr sortorder */
#line 611 "./parser.y"
{GdaSqlSelectOrder *order;
				       order = gda_sql_select_order_new (NULL);
				       order->expr = yymsp[-1].minor.yy70;
				       order->asc = yymsp[0].minor.yy276;
				       yygotominor.yy325 = g_slist_prepend (NULL, order);
}
#line 2754 "parser.c"
        break;
      case 86: /* sortorder ::= ASC */
#line 619 "./parser.y"
{yygotominor.yy276 = TRUE;  yy_destructor(yypParser,5,&yymsp[0].minor);
}
#line 2760 "parser.c"
        break;
      case 87: /* sortorder ::= DESC */
#line 620 "./parser.y"
{yygotominor.yy276 = FALSE;  yy_destructor(yypParser,14,&yymsp[0].minor);
}
#line 2766 "parser.c"
        break;
      case 88: /* sortorder ::= */
#line 621 "./parser.y"
{yygotominor.yy276 = TRUE;}
#line 2771 "parser.c"
        break;
      case 90: /* having_opt ::= HAVING expr */
#line 627 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;  yy_destructor(yypParser,138,&yymsp[-1].minor);
}
#line 2777 "parser.c"
        break;
      case 91: /* groupby_opt ::= */
#line 631 "./parser.y"
{yygotominor.yy333 = 0;}
#line 2782 "parser.c"
        break;
      case 92: /* groupby_opt ::= GROUP BY rnexprlist */
#line 632 "./parser.y"
{yygotominor.yy333 = g_slist_reverse (yymsp[0].minor.yy325);  yy_destructor(yypParser,139,&yymsp[-2].minor);
  yy_destructor(yypParser,137,&yymsp[-1].minor);
}
#line 2789 "parser.c"
        break;
      case 93: /* from ::= */
      case 98: /* stl_prefix ::= */ yytestcase(yyruleno==98);
#line 636 "./parser.y"
{yygotominor.yy191 = NULL;}
#line 2795 "parser.c"
        break;
      case 94: /* from ::= FROM seltablist */
#line 637 "./parser.y"
{yygotominor.yy191 = yymsp[0].minor.yy191;  yy_destructor(yypParser,129,&yymsp[-1].minor);
}
#line 2801 "parser.c"
        break;
      case 95: /* seltablist ::= stl_prefix seltarget on_cond using_opt */
#line 644 "./parser.y"
{
	GSList *last;
	if (yymsp[-3].minor.yy191)
		yygotominor.yy191 = yymsp[-3].minor.yy191;
	else 
		yygotominor.yy191 = gda_sql_select_from_new (NULL);
	gda_sql_select_from_take_new_target (yygotominor.yy191, yymsp[-2].minor.yy134);
	last = g_slist_last (yygotominor.yy191->joins);
	if (last) {
		GdaSqlSelectJoin *join = (GdaSqlSelectJoin *) (last->data);
		join->expr = yymsp[-1].minor.yy70;
		join->position = g_slist_length (yygotominor.yy191->targets) - 1;
		join->use = yymsp[0].minor.yy333;
	}
}
#line 2820 "parser.c"
        break;
      case 96: /* using_opt ::= USING LP inscollist RP */
#line 662 "./parser.y"
{yygotominor.yy333 = yymsp[-1].minor.yy333;  yy_destructor(yypParser,140,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 2828 "parser.c"
        break;
      case 99: /* stl_prefix ::= seltablist jointype */
#line 666 "./parser.y"
{GdaSqlSelectJoin *join;
					      yygotominor.yy191 = yymsp[-1].minor.yy191;
					      join = gda_sql_select_join_new (GDA_SQL_ANY_PART (yygotominor.yy191));
					      join->type = yymsp[0].minor.yy371;
					      gda_sql_select_from_take_new_join (yygotominor.yy191, join);
}
#line 2838 "parser.c"
        break;
      case 100: /* on_cond ::= ON expr */
#line 676 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;  yy_destructor(yypParser,141,&yymsp[-1].minor);
}
#line 2844 "parser.c"
        break;
      case 102: /* jointype ::= COMMA */
#line 680 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_CROSS;  yy_destructor(yypParser,122,&yymsp[0].minor);
}
#line 2850 "parser.c"
        break;
      case 103: /* jointype ::= JOIN */
#line 681 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_INNER;  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2856 "parser.c"
        break;
      case 104: /* jointype ::= CROSS JOIN */
#line 682 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_CROSS;  yy_destructor(yypParser,111,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2863 "parser.c"
        break;
      case 105: /* jointype ::= INNER JOIN */
#line 683 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_INNER;  yy_destructor(yypParser,106,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2870 "parser.c"
        break;
      case 106: /* jointype ::= NATURAL JOIN */
#line 684 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_NATURAL;  yy_destructor(yypParser,107,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2877 "parser.c"
        break;
      case 107: /* jointype ::= LEFT JOIN */
#line 685 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_LEFT;  yy_destructor(yypParser,108,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2884 "parser.c"
        break;
      case 108: /* jointype ::= LEFT OUTER JOIN */
#line 686 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_LEFT;  yy_destructor(yypParser,108,&yymsp[-2].minor);
  yy_destructor(yypParser,142,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2892 "parser.c"
        break;
      case 109: /* jointype ::= RIGHT JOIN */
#line 687 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_RIGHT;  yy_destructor(yypParser,109,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2899 "parser.c"
        break;
      case 110: /* jointype ::= RIGHT OUTER JOIN */
#line 688 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_RIGHT;  yy_destructor(yypParser,109,&yymsp[-2].minor);
  yy_destructor(yypParser,142,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2907 "parser.c"
        break;
      case 111: /* jointype ::= FULL JOIN */
#line 689 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_FULL;  yy_destructor(yypParser,110,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2914 "parser.c"
        break;
      case 112: /* jointype ::= FULL OUTER JOIN */
#line 690 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_FULL;  yy_destructor(yypParser,110,&yymsp[-2].minor);
  yy_destructor(yypParser,142,&yymsp[-1].minor);
  yy_destructor(yypParser,105,&yymsp[0].minor);
}
#line 2922 "parser.c"
        break;
      case 113: /* seltarget ::= fullname as */
#line 695 "./parser.y"
{yygotominor.yy134 = gda_sql_select_target_new (NULL);
				     gda_sql_select_target_take_alias (yygotominor.yy134, yymsp[0].minor.yy0);
				     gda_sql_select_target_take_table_name (yygotominor.yy134, yymsp[-1].minor.yy0);
}
#line 2930 "parser.c"
        break;
      case 114: /* seltarget ::= fullname ID */
#line 699 "./parser.y"
{yygotominor.yy134 = gda_sql_select_target_new (NULL);
                                     gda_sql_select_target_take_alias (yygotominor.yy134, yymsp[0].minor.yy0);
                                     gda_sql_select_target_take_table_name (yygotominor.yy134, yymsp[-1].minor.yy0);
}
#line 2938 "parser.c"
        break;
      case 115: /* seltarget ::= LP compound RP as */
#line 703 "./parser.y"
{yygotominor.yy134 = gda_sql_select_target_new (NULL);
					     gda_sql_select_target_take_alias (yygotominor.yy134, yymsp[0].minor.yy0);
					     gda_sql_select_target_take_select (yygotominor.yy134, yymsp[-2].minor.yy116);
  yy_destructor(yypParser,103,&yymsp[-3].minor);
  yy_destructor(yypParser,104,&yymsp[-1].minor);
}
#line 2948 "parser.c"
        break;
      case 116: /* sclp ::= selcollist COMMA */
#line 713 "./parser.y"
{yygotominor.yy325 = yymsp[-1].minor.yy325;  yy_destructor(yypParser,122,&yymsp[0].minor);
}
#line 2954 "parser.c"
        break;
      case 117: /* sclp ::= */
      case 132: /* rexprlist ::= */ yytestcase(yyruleno==132);
#line 714 "./parser.y"
{yygotominor.yy325 = NULL;}
#line 2960 "parser.c"
        break;
      case 118: /* selcollist ::= sclp expr as */
#line 716 "./parser.y"
{GdaSqlSelectField *field;
					  field = gda_sql_select_field_new (NULL);
					  gda_sql_select_field_take_expr (field, yymsp[-1].minor.yy70);
					  gda_sql_select_field_take_alias (field, yymsp[0].minor.yy0); 
					  yygotominor.yy325 = g_slist_append (yymsp[-2].minor.yy325, field);}
#line 2969 "parser.c"
        break;
      case 119: /* selcollist ::= sclp starname */
#line 721 "./parser.y"
{GdaSqlSelectField *field;
					field = gda_sql_select_field_new (NULL);
					gda_sql_select_field_take_star_value (field, yymsp[0].minor.yy0);
					yygotominor.yy325 = g_slist_append (yymsp[-1].minor.yy325, field);}
#line 2977 "parser.c"
        break;
      case 121: /* starname ::= nm DOT STAR */
      case 196: /* fullname ::= nm DOT nm */ yytestcase(yyruleno==196);
#line 727 "./parser.y"
{gchar *str;
				  str = g_strdup_printf ("%s.%s", g_value_get_string (yymsp[-2].minor.yy0), g_value_get_string (yymsp[0].minor.yy0));
				  yygotominor.yy0 = g_new0 (GValue, 1);
				  g_value_init (yygotominor.yy0, G_TYPE_STRING);
				  g_value_take_string (yygotominor.yy0, str);
				  g_value_reset (yymsp[-2].minor.yy0); g_free (yymsp[-2].minor.yy0);
				  g_value_reset (yymsp[0].minor.yy0); g_free (yymsp[0].minor.yy0);
  yy_destructor(yypParser,143,&yymsp[-1].minor);
}
#line 2991 "parser.c"
        break;
      case 122: /* starname ::= nm DOT nm DOT STAR */
      case 197: /* fullname ::= nm DOT nm DOT nm */ yytestcase(yyruleno==197);
#line 736 "./parser.y"
{gchar *str;
				  str = g_strdup_printf ("%s.%s.%s", g_value_get_string (yymsp[-4].minor.yy0), 
							 g_value_get_string (yymsp[-2].minor.yy0), g_value_get_string (yymsp[0].minor.yy0));
				  yygotominor.yy0 = g_new0 (GValue, 1);
				  g_value_init (yygotominor.yy0, G_TYPE_STRING);
				  g_value_take_string (yygotominor.yy0, str);
				  g_value_reset (yymsp[-4].minor.yy0); g_free (yymsp[-4].minor.yy0);
				  g_value_reset (yymsp[-2].minor.yy0); g_free (yymsp[-2].minor.yy0);
				  g_value_reset (yymsp[0].minor.yy0); g_free (yymsp[0].minor.yy0);
  yy_destructor(yypParser,143,&yymsp[-3].minor);
  yy_destructor(yypParser,143,&yymsp[-1].minor);
}
#line 3008 "parser.c"
        break;
      case 123: /* as ::= AS fullname */
      case 124: /* as ::= AS value */ yytestcase(yyruleno==124);
#line 747 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;  yy_destructor(yypParser,144,&yymsp[-1].minor);
}
#line 3015 "parser.c"
        break;
      case 126: /* distinct ::= */
#line 753 "./parser.y"
{yygotominor.yy297 = NULL;}
#line 3020 "parser.c"
        break;
      case 127: /* distinct ::= ALL */
#line 754 "./parser.y"
{yygotominor.yy297 = NULL;  yy_destructor(yypParser,133,&yymsp[0].minor);
}
#line 3026 "parser.c"
        break;
      case 128: /* distinct ::= DISTINCT */
#line 755 "./parser.y"
{yygotominor.yy297 = g_new0 (Distinct, 1); yygotominor.yy297->distinct = TRUE;  yy_destructor(yypParser,145,&yymsp[0].minor);
}
#line 3032 "parser.c"
        break;
      case 129: /* distinct ::= DISTINCT ON expr */
#line 756 "./parser.y"
{yygotominor.yy297 = g_new0 (Distinct, 1); yygotominor.yy297->distinct = TRUE; yygotominor.yy297->expr = yymsp[0].minor.yy70;  yy_destructor(yypParser,145,&yymsp[-2].minor);
  yy_destructor(yypParser,141,&yymsp[-1].minor);
}
#line 3039 "parser.c"
        break;
      case 130: /* rnexprlist ::= rnexprlist COMMA expr */
      case 133: /* rexprlist ::= rexprlist COMMA expr */ yytestcase(yyruleno==133);
#line 761 "./parser.y"
{yygotominor.yy325 = g_slist_prepend (yymsp[-2].minor.yy325, yymsp[0].minor.yy70);  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 3046 "parser.c"
        break;
      case 131: /* rnexprlist ::= expr */
      case 134: /* rexprlist ::= expr */ yytestcase(yyruleno==134);
#line 762 "./parser.y"
{yygotominor.yy325 = g_slist_append (NULL, yymsp[0].minor.yy70);}
#line 3052 "parser.c"
        break;
      case 135: /* expr ::= pvalue */
#line 774 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;}
#line 3057 "parser.c"
        break;
      case 136: /* expr ::= value */
      case 138: /* expr ::= fullname */ yytestcase(yyruleno==138);
#line 775 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->value = yymsp[0].minor.yy0;}
#line 3063 "parser.c"
        break;
      case 137: /* expr ::= LP expr RP */
#line 776 "./parser.y"
{yygotominor.yy70 = yymsp[-1].minor.yy70;  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3070 "parser.c"
        break;
      case 139: /* expr ::= fullname LP rexprlist RP */
#line 778 "./parser.y"
{GdaSqlFunction *func;
					    yygotominor.yy70 = gda_sql_expr_new (NULL); 
					    func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy70)); 
					    gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					    gda_sql_function_take_args_list (func, g_slist_reverse (yymsp[-1].minor.yy325));
					    yygotominor.yy70->func = func;  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3082 "parser.c"
        break;
      case 140: /* expr ::= fullname LP compound RP */
#line 784 "./parser.y"
{GdaSqlFunction *func;
					     GdaSqlExpr *expr;
					     yygotominor.yy70 = gda_sql_expr_new (NULL); 
					     func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy70)); 
					     gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					     expr = gda_sql_expr_new (GDA_SQL_ANY_PART (func)); 
					     gda_sql_expr_take_select (expr, yymsp[-1].minor.yy116);
					     gda_sql_function_take_args_list (func, g_slist_prepend (NULL, expr));
					     yygotominor.yy70->func = func;  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3097 "parser.c"
        break;
      case 141: /* expr ::= fullname LP starname RP */
#line 793 "./parser.y"
{GdaSqlFunction *func;
					    GdaSqlExpr *expr;
					    yygotominor.yy70 = gda_sql_expr_new (NULL); 
					    func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					    gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					    expr = gda_sql_expr_new (GDA_SQL_ANY_PART (func)); 
					    expr->value = yymsp[-1].minor.yy0;
					    gda_sql_function_take_args_list (func, g_slist_prepend (NULL, expr));
					    yygotominor.yy70->func = func;  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3112 "parser.c"
        break;
      case 142: /* expr ::= CAST LP expr AS fullname RP */
#line 802 "./parser.y"
{yygotominor.yy70 = yymsp[-3].minor.yy70;
						yymsp[-3].minor.yy70->cast_as = g_value_dup_string (yymsp[-1].minor.yy0);
						g_value_reset (yymsp[-1].minor.yy0);
						g_free (yymsp[-1].minor.yy0);  yy_destructor(yypParser,10,&yymsp[-5].minor);
  yy_destructor(yypParser,103,&yymsp[-4].minor);
  yy_destructor(yypParser,144,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3124 "parser.c"
        break;
      case 143: /* expr ::= expr PGCAST fullname */
#line 806 "./parser.y"
{yygotominor.yy70 = yymsp[-2].minor.yy70;
					 yymsp[-2].minor.yy70->cast_as = g_value_dup_string (yymsp[0].minor.yy0);
					 g_value_reset (yymsp[0].minor.yy0);
					 g_free (yymsp[0].minor.yy0);  yy_destructor(yypParser,115,&yymsp[-1].minor);
}
#line 3133 "parser.c"
        break;
      case 144: /* expr ::= expr PLUS|MINUS expr */
#line 811 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (string_to_op_type (yymsp[-1].minor.yy0), yymsp[-2].minor.yy70, yymsp[0].minor.yy70);}
#line 3138 "parser.c"
        break;
      case 145: /* expr ::= expr STAR expr */
#line 812 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_STAR, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,95,&yymsp[-1].minor);
}
#line 3144 "parser.c"
        break;
      case 146: /* expr ::= expr SLASH|REM expr */
      case 147: /* expr ::= expr BITAND|BITOR expr */ yytestcase(yyruleno==147);
      case 153: /* expr ::= expr GT|LEQ|GEQ|LT expr */ yytestcase(yyruleno==153);
      case 154: /* expr ::= expr DIFF|EQ expr */ yytestcase(yyruleno==154);
      case 159: /* expr ::= expr REGEXP|REGEXP_CI|NOT_REGEXP|NOT_REGEXP_CI|SIMILAR expr */ yytestcase(yyruleno==159);
#line 813 "./parser.y"
{yygotominor.yy70 = create_two_expr (string_to_op_type (yymsp[-1].minor.yy0), yymsp[-2].minor.yy70, yymsp[0].minor.yy70);}
#line 3153 "parser.c"
        break;
      case 148: /* expr ::= MINUS expr */
#line 816 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_MINUS, yymsp[0].minor.yy70);  yy_destructor(yypParser,94,&yymsp[-1].minor);
}
#line 3159 "parser.c"
        break;
      case 149: /* expr ::= PLUS expr */
#line 817 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_PLUS, yymsp[0].minor.yy70);  yy_destructor(yypParser,93,&yymsp[-1].minor);
}
#line 3165 "parser.c"
        break;
      case 150: /* expr ::= expr AND expr */
#line 819 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_AND, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,68,&yymsp[-1].minor);
}
#line 3171 "parser.c"
        break;
      case 151: /* expr ::= expr OR expr */
#line 820 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_OR, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,67,&yymsp[-1].minor);
}
#line 3177 "parser.c"
        break;
      case 152: /* expr ::= expr CONCAT expr */
#line 821 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_CONCAT, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,98,&yymsp[-1].minor);
}
#line 3183 "parser.c"
        break;
      case 155: /* expr ::= expr LIKE expr */
#line 825 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_LIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,26,&yymsp[-1].minor);
}
#line 3189 "parser.c"
        break;
      case 156: /* expr ::= expr ILIKE expr */
#line 826 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_ILIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,27,&yymsp[-1].minor);
}
#line 3195 "parser.c"
        break;
      case 157: /* expr ::= expr NOTLIKE expr */
#line 827 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_NOTLIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,71,&yymsp[-1].minor);
}
#line 3201 "parser.c"
        break;
      case 158: /* expr ::= expr NOTILIKE expr */
#line 828 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_NOTILIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,72,&yymsp[-1].minor);
}
#line 3207 "parser.c"
        break;
      case 160: /* expr ::= expr BETWEEN expr AND expr */
#line 830 "./parser.y"
{GdaSqlOperation *cond;
						  yygotominor.yy70 = gda_sql_expr_new (NULL);
						  cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
						  yygotominor.yy70->cond = cond;
						  cond->operator_type = GDA_SQL_OPERATOR_TYPE_BETWEEN;
						  cond->operands = g_slist_append (NULL, yymsp[-4].minor.yy70);
						  GDA_SQL_ANY_PART (yymsp[-4].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
						  cond->operands = g_slist_append (cond->operands, yymsp[-2].minor.yy70);
						  GDA_SQL_ANY_PART (yymsp[-2].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
						  cond->operands = g_slist_append (cond->operands, yymsp[0].minor.yy70);
						  GDA_SQL_ANY_PART (yymsp[0].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,78,&yymsp[-3].minor);
  yy_destructor(yypParser,68,&yymsp[-1].minor);
}
#line 3225 "parser.c"
        break;
      case 161: /* expr ::= expr NOT BETWEEN expr AND expr */
#line 843 "./parser.y"
{GdaSqlOperation *cond;
						      GdaSqlExpr *expr;
						      expr = gda_sql_expr_new (NULL);
						      cond = gda_sql_operation_new (GDA_SQL_ANY_PART (expr));
						      expr->cond = cond;
						      cond->operator_type = GDA_SQL_OPERATOR_TYPE_BETWEEN;
						      cond->operands = g_slist_append (NULL, yymsp[-5].minor.yy70);
						      GDA_SQL_ANY_PART (yymsp[-5].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
						      cond->operands = g_slist_append (cond->operands, yymsp[-2].minor.yy70);
						      GDA_SQL_ANY_PART (yymsp[-2].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
						      cond->operands = g_slist_append (cond->operands, yymsp[0].minor.yy70);
						      GDA_SQL_ANY_PART (yymsp[0].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);

						      yygotominor.yy70 = gda_sql_expr_new (NULL);
						      cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
						      yygotominor.yy70->cond = cond;
						      cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOT;
						      cond->operands = g_slist_prepend (NULL, expr);
						      GDA_SQL_ANY_PART (expr)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,69,&yymsp[-4].minor);
  yy_destructor(yypParser,78,&yymsp[-3].minor);
  yy_destructor(yypParser,68,&yymsp[-1].minor);
}
#line 3252 "parser.c"
        break;
      case 162: /* expr ::= NOT expr */
#line 864 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_NOT, yymsp[0].minor.yy70);  yy_destructor(yypParser,69,&yymsp[-1].minor);
}
#line 3258 "parser.c"
        break;
      case 163: /* expr ::= BITNOT expr */
#line 865 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_BITNOT, yymsp[0].minor.yy70);  yy_destructor(yypParser,102,&yymsp[-1].minor);
}
#line 3264 "parser.c"
        break;
      case 164: /* expr ::= expr uni_op */
#line 866 "./parser.y"
{yygotominor.yy70 = create_uni_expr (yymsp[0].minor.yy147, yymsp[-1].minor.yy70);}
#line 3269 "parser.c"
        break;
      case 165: /* expr ::= expr IS expr */
#line 868 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_IS, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,70,&yymsp[-1].minor);
}
#line 3275 "parser.c"
        break;
      case 166: /* expr ::= LP compound RP */
#line 869 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); gda_sql_expr_take_select (yygotominor.yy70, yymsp[-1].minor.yy116);  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3282 "parser.c"
        break;
      case 167: /* expr ::= expr IN LP rexprlist RP */
#line 870 "./parser.y"
{GdaSqlOperation *cond;
					   GSList *list;
					   yygotominor.yy70 = gda_sql_expr_new (NULL);
					   cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					   yygotominor.yy70->cond = cond;
					   cond->operator_type = GDA_SQL_OPERATOR_TYPE_IN;
					   cond->operands = g_slist_prepend (g_slist_reverse (yymsp[-1].minor.yy325), yymsp[-4].minor.yy70);
					   for (list = cond->operands; list; list = list->next)
						   GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,73,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3299 "parser.c"
        break;
      case 168: /* expr ::= expr IN LP compound RP */
#line 880 "./parser.y"
{GdaSqlOperation *cond;
					    GdaSqlExpr *expr;
					    yygotominor.yy70 = gda_sql_expr_new (NULL);
					    cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					    yygotominor.yy70->cond = cond;
					    cond->operator_type = GDA_SQL_OPERATOR_TYPE_IN;
					    
					    expr = gda_sql_expr_new (GDA_SQL_ANY_PART (cond));
					    gda_sql_expr_take_select (expr, yymsp[-1].minor.yy116);
					    cond->operands = g_slist_prepend (NULL, expr);
					    cond->operands = g_slist_prepend (cond->operands, yymsp[-4].minor.yy70);
					    GDA_SQL_ANY_PART (yymsp[-4].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,73,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3319 "parser.c"
        break;
      case 169: /* expr ::= expr NOT IN LP rexprlist RP */
#line 893 "./parser.y"
{GdaSqlOperation *cond;
					       GSList *list;
					       yygotominor.yy70 = gda_sql_expr_new (NULL);
					       cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					       yygotominor.yy70->cond = cond;
					       cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOTIN;
					       cond->operands = g_slist_prepend (g_slist_reverse (yymsp[-1].minor.yy325), yymsp[-5].minor.yy70);
					       for (list = cond->operands; list; list = list->next)
						       GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,69,&yymsp[-4].minor);
  yy_destructor(yypParser,73,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3337 "parser.c"
        break;
      case 170: /* expr ::= expr NOT IN LP compound RP */
#line 903 "./parser.y"
{GdaSqlOperation *cond;
					       GdaSqlExpr *expr;
					       yygotominor.yy70 = gda_sql_expr_new (NULL);
					       cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					       yygotominor.yy70->cond = cond;
					       cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOTIN;
					       
					       expr = gda_sql_expr_new (GDA_SQL_ANY_PART (cond));
					       gda_sql_expr_take_select (expr, yymsp[-1].minor.yy116);
					       cond->operands = g_slist_prepend (NULL, expr);
					       cond->operands = g_slist_prepend (cond->operands, yymsp[-5].minor.yy70);
					       GDA_SQL_ANY_PART (yymsp[-5].minor.yy70)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,69,&yymsp[-4].minor);
  yy_destructor(yypParser,73,&yymsp[-3].minor);
  yy_destructor(yypParser,103,&yymsp[-2].minor);
  yy_destructor(yypParser,104,&yymsp[0].minor);
}
#line 3358 "parser.c"
        break;
      case 171: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 916 "./parser.y"
{
	GdaSqlCase *sc;
	GSList *list;
	yygotominor.yy70 = gda_sql_expr_new (NULL);
	sc = gda_sql_case_new (GDA_SQL_ANY_PART (yygotominor.yy70));
	sc->base_expr = yymsp[-3].minor.yy146;
	sc->else_expr = yymsp[-1].minor.yy146;
	sc->when_expr_list = yymsp[-2].minor.yy59.when_list;
	sc->then_expr_list = yymsp[-2].minor.yy59.then_list;
	yygotominor.yy70->case_s = sc;
	for (list = sc->when_expr_list; list; list = list->next)
		GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (sc);
	for (list = sc->then_expr_list; list; list = list->next)
		GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (sc);
  yy_destructor(yypParser,146,&yymsp[-4].minor);
  yy_destructor(yypParser,17,&yymsp[0].minor);
}
#line 3379 "parser.c"
        break;
      case 172: /* case_operand ::= expr */
#line 934 "./parser.y"
{yygotominor.yy146 = yymsp[0].minor.yy70;}
#line 3384 "parser.c"
        break;
      case 173: /* case_operand ::= */
      case 177: /* case_else ::= */ yytestcase(yyruleno==177);
#line 935 "./parser.y"
{yygotominor.yy146 = NULL;}
#line 3390 "parser.c"
        break;
      case 174: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 941 "./parser.y"
{
	yygotominor.yy59.when_list = g_slist_append (yymsp[-4].minor.yy59.when_list, yymsp[-2].minor.yy70);
	yygotominor.yy59.then_list = g_slist_append (yymsp[-4].minor.yy59.then_list, yymsp[0].minor.yy70);
  yy_destructor(yypParser,147,&yymsp[-3].minor);
  yy_destructor(yypParser,148,&yymsp[-1].minor);
}
#line 3400 "parser.c"
        break;
      case 175: /* case_exprlist ::= WHEN expr THEN expr */
#line 945 "./parser.y"
{
	yygotominor.yy59.when_list = g_slist_prepend (NULL, yymsp[-2].minor.yy70);
	yygotominor.yy59.then_list = g_slist_prepend (NULL, yymsp[0].minor.yy70);
  yy_destructor(yypParser,147,&yymsp[-3].minor);
  yy_destructor(yypParser,148,&yymsp[-1].minor);
}
#line 3410 "parser.c"
        break;
      case 176: /* case_else ::= ELSE expr */
#line 952 "./parser.y"
{yygotominor.yy146 = yymsp[0].minor.yy70;  yy_destructor(yypParser,149,&yymsp[-1].minor);
}
#line 3416 "parser.c"
        break;
      case 178: /* uni_op ::= ISNULL */
#line 956 "./parser.y"
{yygotominor.yy147 = GDA_SQL_OPERATOR_TYPE_ISNULL;  yy_destructor(yypParser,74,&yymsp[0].minor);
}
#line 3422 "parser.c"
        break;
      case 179: /* uni_op ::= IS NOTNULL */
#line 957 "./parser.y"
{yygotominor.yy147 = GDA_SQL_OPERATOR_TYPE_ISNOTNULL;  yy_destructor(yypParser,70,&yymsp[-1].minor);
  yy_destructor(yypParser,75,&yymsp[0].minor);
}
#line 3429 "parser.c"
        break;
      case 180: /* value ::= NULL */
#line 961 "./parser.y"
{yygotominor.yy0 = NULL;  yy_destructor(yypParser,150,&yymsp[0].minor);
}
#line 3435 "parser.c"
        break;
      case 184: /* pvalue ::= UNSPECVAL LSBRACKET paramspec RSBRACKET */
#line 970 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->param_spec = yymsp[-1].minor.yy339;  yy_destructor(yypParser,152,&yymsp[-3].minor);
  yy_destructor(yypParser,153,&yymsp[-2].minor);
  yy_destructor(yypParser,154,&yymsp[0].minor);
}
#line 3443 "parser.c"
        break;
      case 185: /* pvalue ::= value LSBRACKET paramspec RSBRACKET */
#line 971 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->value = yymsp[-3].minor.yy0; yygotominor.yy70->param_spec = yymsp[-1].minor.yy339;  yy_destructor(yypParser,153,&yymsp[-2].minor);
  yy_destructor(yypParser,154,&yymsp[0].minor);
}
#line 3450 "parser.c"
        break;
      case 186: /* pvalue ::= SIMPLEPARAM */
#line 972 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->param_spec = gda_sql_param_spec_new (yymsp[0].minor.yy0);}
#line 3455 "parser.c"
        break;
      case 187: /* paramspec ::= */
#line 977 "./parser.y"
{yygotominor.yy339 = NULL;}
#line 3460 "parser.c"
        break;
      case 188: /* paramspec ::= paramspec PNAME */
#line 978 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					 gda_sql_param_spec_take_name (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3466 "parser.c"
        break;
      case 189: /* paramspec ::= paramspec PDESCR */
#line 980 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					 gda_sql_param_spec_take_descr (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3472 "parser.c"
        break;
      case 190: /* paramspec ::= paramspec PTYPE */
#line 982 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					 gda_sql_param_spec_take_type (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3478 "parser.c"
        break;
      case 191: /* paramspec ::= paramspec PNULLOK */
#line 984 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					   gda_sql_param_spec_take_nullok (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3484 "parser.c"
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
  gda_lemon_postgres_parserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
  gda_lemon_postgres_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
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
  gda_lemon_postgres_parserARG_FETCH;
#define TOKEN (yyminor.yy0)
#line 22 "./parser.y"

	gda_sql_parser_set_syntax_error (pdata->parser);
#line 3551 "parser.c"
  gda_lemon_postgres_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  gda_lemon_postgres_parserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
  gda_lemon_postgres_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "gda_lemon_postgres_parserAlloc" which describes the current state of the parser.
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
void gda_lemon_postgres_parser(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  gda_lemon_postgres_parserTOKENTYPE yyminor       /* The value for the token */
  gda_lemon_postgres_parserARG_PDECL               /* Optional %extra_argument parameter */
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
  gda_lemon_postgres_parserARG_STORE;

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
