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
**    priv_gda_sql_parserTOKENTYPE     is the data type used for minor tokens given 
**                       directly to the parser from the tokenizer.
**    YYMINORTYPE        is the data type used for all minor tokens.
**                       This is typically a union of many types, one of
**                       which is priv_gda_sql_parserTOKENTYPE.  The entry in the union
**                       for base tokens is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    priv_gda_sql_parserARG_SDECL     A static variable declaration for the %extra_argument
**    priv_gda_sql_parserARG_PDECL     A parameter declaration for the %extra_argument
**    priv_gda_sql_parserARG_STORE     Code to store %extra_argument into yypParser
**    priv_gda_sql_parserARG_FETCH     Code to extract %extra_argument from yypParser
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
*/
#define YYCODETYPE unsigned char
#define YYNOCODE 211
#define YYACTIONTYPE unsigned short int
#define priv_gda_sql_parserTOKENTYPE GValue *
typedef union {
  int yyinit;
  priv_gda_sql_parserTOKENTYPE yy0;
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
#define priv_gda_sql_parserARG_SDECL GdaSqlParserIface *pdata;
#define priv_gda_sql_parserARG_PDECL ,GdaSqlParserIface *pdata
#define priv_gda_sql_parserARG_FETCH GdaSqlParserIface *pdata = yypParser->pdata
#define priv_gda_sql_parserARG_STORE yypParser->pdata = pdata
#define YYNSTATE 367
#define YYNRULE 200
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
#define YY_ACTTAB_COUNT (1395)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    41,   40,  367,  246,  246,  142,  243,   22,   51,   49,
 /*    10 */    49,   44,  103,  368,   36,   43,   43,   43,   43,   37,
 /*    20 */    37,   37,   37,   37,  138,   47,   47,  160,   77,   52,
 /*    30 */    52,   51,   49,   49,   44,  244,  354,  353,  165,  352,
 /*    40 */   351,  350,  349,   45,   46,  203,   13,   39,   38,  238,
 /*    50 */   339,   77,   42,   42,   36,   43,   43,   43,   43,   37,
 /*    60 */    37,   37,   37,   37,  175,   47,   47,  323,  298,   52,
 /*    70 */    52,   51,   49,   49,   44,   84,  322,  318,  228,  227,
 /*    80 */   200,  199,  198,  229,  194,  182,  282,  195,   41,   40,
 /*    90 */   219,   77,  246,  319,  243,  276,  202,   21,  157,  328,
 /*   100 */   104,  278,  363,   43,   43,   43,   43,   37,   37,   37,
 /*   110 */    37,   37,  340,   47,   47,  137,   50,   52,   52,   51,
 /*   120 */    49,   49,   44,  244,  354,  263,  262,  193,   28,   50,
 /*   130 */    26,   45,   46,  203,   13,   39,   38,  238,  339,   77,
 /*   140 */    42,   42,   36,   43,   43,   43,   43,   37,   37,   37,
 /*   150 */    37,   37,    1,   47,   47,  280,  279,   52,   52,   51,
 /*   160 */    49,   49,   44,   41,   40,   82,   37,  128,   47,   47,
 /*   170 */   286,  285,   52,   52,   51,   49,   49,   44,  221,   77,
 /*   180 */   186,   52,   52,   51,   49,   49,   44,  333,  213,  352,
 /*   190 */   351,  350,  349,  127,   77,  147,  358,  363,  173,  172,
 /*   200 */   171,  366,  365,   77,  366,  365,   45,   46,  203,   13,
 /*   210 */    39,   38,  238,  339,  329,   42,   42,   36,   43,   43,
 /*   220 */    43,   43,   37,   37,   37,   37,   37,   48,   47,   47,
 /*   230 */   343,   50,   52,   52,   51,   49,   49,   44,   41,   40,
 /*   240 */   314,    3,  246,  348,  243,   37,   37,   37,   37,   37,
 /*   250 */    63,   47,   47,  312,   77,   52,   52,   51,   49,   49,
 /*   260 */    44,  232,  357,  336,  173,  172,  171,  235,  303,  246,
 /*   270 */   364,  305,   34,  244,  354,  197,  226,   77,  173,  172,
 /*   280 */   171,   45,   46,  203,   13,   39,   38,  238,  339,  225,
 /*   290 */    42,   42,   36,   43,   43,   43,   43,   37,   37,   37,
 /*   300 */    37,   37,  359,   47,   47,  146,  306,   52,   52,   51,
 /*   310 */    49,   49,   44,  183,  338,   41,   40,  184,    2,  335,
 /*   320 */   242,    6,  243,   75,  204,  173,  172,  171,  104,   77,
 /*   330 */   363,  356,  370,  291,  140,  136,  267,  266,  289,  278,
 /*   340 */   370,  370,  370,  287,  265,  264,  337,  334,  288,  327,
 /*   350 */   234,  244,  354,  299,  358,  122,   59,   76,   45,   46,
 /*   360 */   203,   13,   39,   38,  238,  339,  310,   42,   42,   36,
 /*   370 */    43,   43,   43,   43,   37,   37,   37,   37,   37,  196,
 /*   380 */    47,   47,   66,  181,   52,   52,   51,   49,   49,   44,
 /*   390 */   246,  156,  243,  246,  201,  305,   41,   40,  104,  189,
 /*   400 */   363,  242,  224,  243,  174,  246,   77,  243,  246,   65,
 /*   410 */   205,  178,   57,   95,   11,  215,  218,  139,   20,  347,
 /*   420 */   357,  244,  354,   56,  154,   10,  230,  173,  172,  171,
 /*   430 */   151,  296,  244,  354,  131,  124,  244,  354,   27,   45,
 /*   440 */    46,  203,   13,   39,   38,  238,  339,   50,   42,   42,
 /*   450 */    36,   43,   43,   43,   43,   37,   37,   37,   37,   37,
 /*   460 */   359,   47,   47,   58,  134,   52,   52,   51,   49,   49,
 /*   470 */    44,   18,  403,  180,  295,   72,  185,   41,   40,   55,
 /*   480 */   246,  378,  243,  246,  254,  243,  222,   77,   90,  356,
 /*   490 */   363,   90,  317,  363,   68,  378,  378,   57,  284,  271,
 /*   500 */   153,   50,   53,  208,  261,  278,  149,  277,  246,  147,
 /*   510 */   360,  244,  354,  278,  244,  354,  358,  316,  315,   24,
 /*   520 */    45,   46,  203,   13,   39,   38,  238,  339,  358,   42,
 /*   530 */    42,   36,   43,   43,   43,   43,   37,   37,   37,   37,
 /*   540 */    37,  313,   47,   47,  284,  311,   52,   52,   51,   49,
 /*   550 */    49,   44,  289,  273,   41,   40,  246,  287,  243,  278,
 /*   560 */   192,  341,  288,  246,  104,  239,  377,  270,   77,  173,
 /*   570 */   172,  171,  293,  278,  253,  132,  330,   16,  403,  309,
 /*   580 */   377,  377,  357,  284,  173,  172,  171,  244,  354,  363,
 /*   590 */   220,   50,   62,  196,  357,  292,   57,   45,   46,  203,
 /*   600 */    13,   39,   38,  238,  339,  358,   42,   42,   36,   43,
 /*   610 */    43,   43,   43,   37,   37,   37,   37,   37,  188,   47,
 /*   620 */    47,  281,  359,   52,   52,   51,   49,   49,   44,   41,
 /*   630 */    40,  252,  217,  246,  359,  243,   44,  191,  246,  186,
 /*   640 */   243,  104,  190,  362,  216,   77,  104,  269,   75,   74,
 /*   650 */   284,  356,  246,   77,  307,  246,  363,  152,  155,   71,
 /*   660 */   363,   54,  260,  356,  244,  354,  358,   82,  278,  244,
 /*   670 */   354,  357,   45,   35,  203,   13,   39,   38,  238,  339,
 /*   680 */   358,   42,   42,   36,   43,   43,   43,   43,   37,   37,
 /*   690 */    37,   37,   37,    9,   47,   47,  211,  147,   52,   52,
 /*   700 */    51,   49,   49,   44,   41,   40,  246,  246,  243,  150,
 /*   710 */   246,  359,  243,  246,  108,  243,  361,  246,  126,  207,
 /*   720 */    77,  143,  179,    8,    7,  130,  326,  251,  246,  125,
 /*   730 */   206,   70,  357,  363,  173,  172,  171,  244,  354,  363,
 /*   740 */   356,  244,  354,   60,  244,  354,  357,   45,   33,  203,
 /*   750 */    13,   39,   38,  238,  339,  324,   42,   42,   36,   43,
 /*   760 */    43,   43,   43,   37,   37,   37,   37,   37,  342,   47,
 /*   770 */    47,   15,  359,   52,   52,   51,   49,   49,   44,   41,
 /*   780 */    40,  246,  187,  243,   79,  246,  359,  243,  246,  107,
 /*   790 */   243,   14,  297,  144,  141,   77,   89,  342,  321,  363,
 /*   800 */   177,  356,  173,  172,  171,  176,   83,  294,   81,   80,
 /*   810 */   164,  342,  244,  354,   53,  356,  244,  354,   77,  244,
 /*   820 */   354,  145,   25,   46,  203,   13,   39,   38,  238,  339,
 /*   830 */   358,   42,   42,   36,   43,   43,   43,   43,   37,   37,
 /*   840 */    37,   37,   37,  342,   47,   47,  163,  231,   52,   52,
 /*   850 */    51,   49,   49,   44,   41,   40,  246,  162,  243,  246,
 /*   860 */    19,  243,  233,  246,  123,  243,  161,  112,   23,  159,
 /*   870 */    77,  109,  358,   17,  358,  325,  223,  568,  148,  308,
 /*   880 */   129,  240,  283,  220,  275,  272,  214,  244,  354,   73,
 /*   890 */   244,  354,  178,  268,  244,  354,  357,  363,   67,  203,
 /*   900 */    13,   39,   38,  238,  339,  209,   42,   42,   36,   43,
 /*   910 */    43,   43,   43,   37,   37,   37,   37,   37,   78,   47,
 /*   920 */    47,   61,  358,   52,   52,   51,   49,   49,   44,  212,
 /*   930 */   246,  240,  302,  210,  250,   69,  359,  248,  357,  336,
 /*   940 */   357,   85,   30,  158,  358,   77,  358,  358,  237,  304,
 /*   950 */   246,  170,  243,  240,  246,  249,  243,  245,  169,  289,
 /*   960 */   300,  301,  120,  355,  287,  356,   31,   32,  346,  288,
 /*   970 */   345,  135,  290,  133,  274,   29,    4,  247,  359,  320,
 /*   980 */   359,  244,  354,  258,  257,  244,  354,  256,  357,  336,
 /*   990 */   241,  259,   30,  255,  358,  335,  569,  220,  331,  569,
 /*  1000 */   196,  569,  569,  240,  569,  569,  147,  356,  569,  356,
 /*  1010 */   357,  336,  357,  357,   30,  569,   31,   32,   12,  569,
 /*  1020 */   569,  569,  337,  334,  236,   29,    5,  332,  359,  569,
 /*  1030 */   569,  569,  569,  569,  569,  569,  569,  569,   31,   32,
 /*  1040 */   346,  569,  344,  569,  569,  335,  569,   29,    5,  569,
 /*  1050 */   359,  569,  359,  359,  246,  569,  243,  356,  569,  569,
 /*  1060 */   357,  336,  118,  569,   30,  569,  358,  335,   12,  569,
 /*  1070 */   569,  569,  337,  334,  236,  240,  246,  332,  243,  356,
 /*  1080 */   569,  356,  356,  569,  117,  244,  354,  569,   31,   32,
 /*  1090 */    12,  569,  569,  569,  337,  334,  236,   29,    4,  332,
 /*  1100 */   359,  569,  569,  569,  569,  569,  569,  244,  354,  569,
 /*  1110 */   569,  246,  569,  243,  569,  569,  569,  335,  569,  116,
 /*  1120 */   569,  569,  569,  246,  569,  243,  569,  569,  147,  356,
 /*  1130 */   569,  115,  357,  336,  569,  569,   30,  569,  569,  569,
 /*  1140 */    12,  569,  244,  354,  337,  334,  236,  569,  246,  332,
 /*  1150 */   243,  246,  569,  243,  244,  354,  114,  569,  569,  121,
 /*  1160 */    31,   32,  569,  569,  569,  569,  569,  569,  569,   29,
 /*  1170 */     5,  569,  359,  246,  569,  243,  246,  569,  243,  244,
 /*  1180 */   354,  106,  244,  354,  119,  569,  569,  569,  569,  335,
 /*  1190 */   569,  246,  569,  243,  569,  246,  569,  243,  246,  105,
 /*  1200 */   243,  356,  569,  111,  244,  354,  168,  244,  354,  569,
 /*  1210 */   569,  246,   12,  243,  569,  569,  337,  334,  236,  167,
 /*  1220 */   569,  332,  244,  354,  569,  569,  244,  354,  569,  244,
 /*  1230 */   354,  569,  569,  246,  569,  243,  569,  569,  569,  569,
 /*  1240 */   569,  110,  244,  354,  569,  246,  569,  243,  246,  569,
 /*  1250 */   243,  569,  246,  166,  243,  246,   88,  243,  569,  246,
 /*  1260 */   102,  243,  569,  101,  244,  354,  569,   87,  569,  569,
 /*  1270 */   246,  569,  243,  246,  569,  243,  244,  354,  100,  244,
 /*  1280 */   354,   86,  569,  244,  354,  569,  244,  354,  569,  569,
 /*  1290 */   244,  354,  246,  569,  243,  569,  569,  569,  569,  569,
 /*  1300 */    99,  244,  354,  569,  244,  354,  246,  569,  243,  246,
 /*  1310 */   569,  243,  569,  246,   98,  243,  569,   64,  246,  569,
 /*  1320 */   243,   97,  569,  244,  354,  569,   96,  246,  569,  243,
 /*  1330 */   569,  569,  569,  569,  246,   94,  243,  244,  354,  569,
 /*  1340 */   244,  354,   93,  569,  244,  354,  246,  569,  243,  244,
 /*  1350 */   354,  569,  569,  246,   92,  243,  569,  569,  244,  354,
 /*  1360 */   569,   91,  246,  569,  243,  244,  354,  569,  569,  569,
 /*  1370 */   113,  569,  569,  569,  569,  569,  569,  244,  354,  569,
 /*  1380 */   569,  569,  569,  569,  244,  354,  569,  569,  569,  569,
 /*  1390 */   569,  569,  569,  244,  354,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */    26,   27,    0,  172,  172,  174,  174,   33,   97,   98,
 /*    10 */    99,  100,  180,    0,   80,   81,   82,   83,   84,   85,
 /*    20 */    86,   87,   88,   89,  144,   91,   92,  196,  117,   95,
 /*    30 */    96,   97,   98,   99,  100,  203,  204,  155,  206,  157,
 /*    40 */   158,  159,  160,   69,   70,   71,   72,   73,   74,   75,
 /*    50 */    76,  117,   78,   79,   80,   81,   82,   83,   84,   85,
 /*    60 */    86,   87,   88,   89,  154,   91,   92,    5,  123,   95,
 /*    70 */    96,   97,   98,   99,  100,  130,   14,  107,  108,  109,
 /*    80 */   110,  111,  112,  113,   23,  165,   53,   54,   26,   27,
 /*    90 */    57,  117,  172,  123,  174,  166,  176,  123,  169,  106,
 /*   100 */   180,  172,  182,   81,   82,   83,   84,   85,   86,   87,
 /*   110 */    88,   89,  106,   91,   92,  144,  123,   95,   96,   97,
 /*   120 */    98,   99,  100,  203,  204,   64,   65,   66,  148,  123,
 /*   130 */   150,   69,   70,   71,   72,   73,   74,   75,   76,  117,
 /*   140 */    78,   79,   80,   81,   82,   83,   84,   85,   86,   87,
 /*   150 */    88,   89,  105,   91,   92,   55,   56,   95,   96,   97,
 /*   160 */    98,   99,  100,   26,   27,  105,   89,  144,   91,   92,
 /*   170 */    58,   59,   95,   96,   97,   98,   99,  100,  163,  117,
 /*   180 */   165,   95,   96,   97,   98,   99,  100,  155,  128,  157,
 /*   190 */   158,  159,  160,  144,  117,  135,    1,  182,  114,  115,
 /*   200 */   116,  120,  121,  117,  120,  121,   69,   70,   71,   72,
 /*   210 */    73,   74,   75,   76,  106,   78,   79,   80,   81,   82,
 /*   220 */    83,   84,   85,   86,   87,   88,   89,  105,   91,   92,
 /*   230 */   106,  123,   95,   96,   97,   98,   99,  100,   26,   27,
 /*   240 */   107,  105,  172,  106,  174,   85,   86,   87,   88,   89,
 /*   250 */   180,   91,   92,  107,  117,   95,   96,   97,   98,   99,
 /*   260 */   100,  191,   67,   68,  114,  115,  116,   75,    1,  172,
 /*   270 */   106,  174,   80,  203,  204,  178,  143,  117,  114,  115,
 /*   280 */   116,   69,   70,   71,   72,   73,   74,   75,   76,  143,
 /*   290 */    78,   79,   80,   81,   82,   83,   84,   85,   86,   87,
 /*   300 */    88,   89,  107,   91,   92,  185,  106,   95,   96,   97,
 /*   310 */    98,   99,  100,  165,   17,   26,   27,  106,  105,  124,
 /*   320 */   172,  201,  174,  123,  176,  114,  115,  116,  180,  117,
 /*   330 */   182,  136,  106,  166,  167,  168,   64,   65,   13,  172,
 /*   340 */   114,  115,  116,   18,   64,   65,  151,  152,   23,  106,
 /*   350 */   202,  203,  204,    1,    1,  194,  195,  145,   69,   70,
 /*   360 */    71,   72,   73,   74,   75,   76,  107,   78,   79,   80,
 /*   370 */    81,   82,   83,   84,   85,   86,   87,   88,   89,   54,
 /*   380 */    91,   92,    8,  165,   95,   96,   97,   98,   99,  100,
 /*   390 */   172,   17,  174,  172,  176,  174,   26,   27,  180,  178,
 /*   400 */   182,  172,  143,  174,  154,  172,  117,  174,  172,  180,
 /*   410 */   174,   58,  145,  180,  138,   62,   63,  181,  123,  106,
 /*   420 */    67,  203,  204,   49,   50,  138,  193,  114,  115,  116,
 /*   430 */   106,  202,  203,  204,   60,   61,  203,  204,  149,   69,
 /*   440 */    70,   71,   72,   73,   74,   75,   76,  123,   78,   79,
 /*   450 */    80,   81,   82,   83,   84,   85,   86,   87,   88,   89,
 /*   460 */   107,   91,   92,  167,  168,   95,   96,   97,   98,   99,
 /*   470 */   100,  123,   51,  165,  134,  122,  165,   26,   27,  105,
 /*   480 */   172,  106,  174,  172,  106,  174,  146,  117,  180,  136,
 /*   490 */   182,  180,  107,  182,  123,  120,  121,  145,  123,  166,
 /*   500 */   126,  123,  131,  129,  171,  172,  132,  166,  172,  135,
 /*   510 */   174,  203,  204,  172,  203,  204,    1,  107,  107,  149,
 /*   520 */    69,   70,   71,   72,   73,   74,   75,   76,    1,   78,
 /*   530 */    79,   80,   81,   82,   83,   84,   85,   86,   87,   88,
 /*   540 */    89,  107,   91,   92,  123,  107,   95,   96,   97,   98,
 /*   550 */    99,  100,   13,  166,   26,   27,  172,   18,  174,  172,
 /*   560 */   176,  106,   23,  172,  180,  174,  106,  166,  117,  114,
 /*   570 */   115,  116,  165,  172,  106,   60,  106,  142,   51,  107,
 /*   580 */   120,  121,   67,  123,  114,  115,  116,  203,  204,  182,
 /*   590 */    51,  123,  105,   54,   67,  106,  145,   69,   70,   71,
 /*   600 */    72,   73,   74,   75,   76,    1,   78,   79,   80,   81,
 /*   610 */    82,   83,   84,   85,   86,   87,   88,   89,   52,   91,
 /*   620 */    92,   54,  107,   95,   96,   97,   98,   99,  100,   26,
 /*   630 */    27,  106,   68,  172,  107,  174,  100,  176,  172,  165,
 /*   640 */   174,  180,  176,  165,  123,  117,  180,  124,  123,  122,
 /*   650 */   123,  136,  172,  117,  174,  172,  182,  174,   68,  127,
 /*   660 */   182,  122,  166,  136,  203,  204,    1,  105,  172,  203,
 /*   670 */   204,   67,   69,   70,   71,   72,   73,   74,   75,   76,
 /*   680 */     1,   78,   79,   80,   81,   82,   83,   84,   85,   86,
 /*   690 */    87,   88,   89,  105,   91,   92,  123,  135,   95,   96,
 /*   700 */    97,   98,   99,  100,   26,   27,  172,  172,  174,  174,
 /*   710 */   172,  107,  174,  172,  180,  174,  165,  172,  180,  174,
 /*   720 */   117,  180,  165,  105,  105,   60,  106,    1,  172,  125,
 /*   730 */   174,  130,   67,  182,  114,  115,  116,  203,  204,  182,
 /*   740 */   136,  203,  204,  133,  203,  204,   67,   69,   70,   71,
 /*   750 */    72,   73,   74,   75,   76,  192,   78,   79,   80,   81,
 /*   760 */    82,   83,   84,   85,   86,   87,   88,   89,  205,   91,
 /*   770 */    92,   79,  107,   95,   96,   97,   98,   99,  100,   26,
 /*   780 */    27,  172,  165,  174,  105,  172,  107,  174,  172,  180,
 /*   790 */   174,   79,  200,  180,  106,  117,  180,  205,  192,  182,
 /*   800 */   209,  136,  114,  115,  116,  209,  183,  134,  183,  183,
 /*   810 */   179,  205,  203,  204,  131,  136,  203,  204,  117,  203,
 /*   820 */   204,  207,  148,   70,   71,   72,   73,   74,   75,   76,
 /*   830 */     1,   78,   79,   80,   81,   82,   83,   84,   85,   86,
 /*   840 */    87,   88,   89,  205,   91,   92,  187,  140,   95,   96,
 /*   850 */    97,   98,   99,  100,   26,   27,  172,  188,  174,  172,
 /*   860 */   139,  174,  137,  172,  180,  174,  189,  180,  136,  197,
 /*   870 */   117,  180,    1,  142,    1,  190,  141,  162,  163,  198,
 /*   880 */   165,   10,  168,   51,  168,  122,  173,  203,  204,  170,
 /*   890 */   203,  204,   58,  171,  203,  204,   67,  182,  170,   71,
 /*   900 */    72,   73,   74,   75,   76,   69,   78,   79,   80,   81,
 /*   910 */    82,   83,   84,   85,   86,   87,   88,   89,  175,   91,
 /*   920 */    92,  105,    1,   95,   96,   97,   98,   99,  100,  177,
 /*   930 */   172,   10,  174,  123,  179,  173,  107,  164,   67,   68,
 /*   940 */    67,  184,   71,  169,    1,  117,    1,    1,  208,  200,
 /*   950 */   172,  186,  174,   10,  172,  179,  174,  172,  180,   13,
 /*   960 */   200,  203,  180,  172,   18,  136,   95,   96,   97,   23,
 /*   970 */    97,  169,  167,  169,  167,  104,  105,  164,  107,  199,
 /*   980 */   107,  203,  204,  172,  172,  203,  204,  172,   67,   68,
 /*   990 */   172,  172,   71,  172,    1,  124,  210,   51,   77,  210,
 /*  1000 */    54,  210,  210,   10,  210,  210,  135,  136,  210,  136,
 /*  1010 */    67,   68,   67,   67,   71,  210,   95,   96,  147,  210,
 /*  1020 */   210,  210,  151,  152,  153,  104,  105,  156,  107,  210,
 /*  1030 */   210,  210,  210,  210,  210,  210,  210,  210,   95,   96,
 /*  1040 */    97,  210,   97,  210,  210,  124,  210,  104,  105,  210,
 /*  1050 */   107,  210,  107,  107,  172,  210,  174,  136,  210,  210,
 /*  1060 */    67,   68,  180,  210,   71,  210,    1,  124,  147,  210,
 /*  1070 */   210,  210,  151,  152,  153,   10,  172,  156,  174,  136,
 /*  1080 */   210,  136,  136,  210,  180,  203,  204,  210,   95,   96,
 /*  1090 */   147,  210,  210,  210,  151,  152,  153,  104,  105,  156,
 /*  1100 */   107,  210,  210,  210,  210,  210,  210,  203,  204,  210,
 /*  1110 */   210,  172,  210,  174,  210,  210,  210,  124,  210,  180,
 /*  1120 */   210,  210,  210,  172,  210,  174,  210,  210,  135,  136,
 /*  1130 */   210,  180,   67,   68,  210,  210,   71,  210,  210,  210,
 /*  1140 */   147,  210,  203,  204,  151,  152,  153,  210,  172,  156,
 /*  1150 */   174,  172,  210,  174,  203,  204,  180,  210,  210,  180,
 /*  1160 */    95,   96,  210,  210,  210,  210,  210,  210,  210,  104,
 /*  1170 */   105,  210,  107,  172,  210,  174,  172,  210,  174,  203,
 /*  1180 */   204,  180,  203,  204,  180,  210,  210,  210,  210,  124,
 /*  1190 */   210,  172,  210,  174,  210,  172,  210,  174,  172,  180,
 /*  1200 */   174,  136,  210,  180,  203,  204,  180,  203,  204,  210,
 /*  1210 */   210,  172,  147,  174,  210,  210,  151,  152,  153,  180,
 /*  1220 */   210,  156,  203,  204,  210,  210,  203,  204,  210,  203,
 /*  1230 */   204,  210,  210,  172,  210,  174,  210,  210,  210,  210,
 /*  1240 */   210,  180,  203,  204,  210,  172,  210,  174,  172,  210,
 /*  1250 */   174,  210,  172,  180,  174,  172,  180,  174,  210,  172,
 /*  1260 */   180,  174,  210,  180,  203,  204,  210,  180,  210,  210,
 /*  1270 */   172,  210,  174,  172,  210,  174,  203,  204,  180,  203,
 /*  1280 */   204,  180,  210,  203,  204,  210,  203,  204,  210,  210,
 /*  1290 */   203,  204,  172,  210,  174,  210,  210,  210,  210,  210,
 /*  1300 */   180,  203,  204,  210,  203,  204,  172,  210,  174,  172,
 /*  1310 */   210,  174,  210,  172,  180,  174,  210,  180,  172,  210,
 /*  1320 */   174,  180,  210,  203,  204,  210,  180,  172,  210,  174,
 /*  1330 */   210,  210,  210,  210,  172,  180,  174,  203,  204,  210,
 /*  1340 */   203,  204,  180,  210,  203,  204,  172,  210,  174,  203,
 /*  1350 */   204,  210,  210,  172,  180,  174,  210,  210,  203,  204,
 /*  1360 */   210,  180,  172,  210,  174,  203,  204,  210,  210,  210,
 /*  1370 */   180,  210,  210,  210,  210,  210,  210,  203,  204,  210,
 /*  1380 */   210,  210,  210,  210,  203,  204,  210,  210,  210,  210,
 /*  1390 */   210,  210,  210,  203,  204,
};
#define YY_SHIFT_USE_DFLT (-121)
#define YY_SHIFT_COUNT (248)
#define YY_SHIFT_MIN   (-120)
#define YY_SHIFT_MAX   (1065)
static const short yy_shift_ofst[] = {
 /*     0 */   374,  871,  993,  993,  993,  993,  943, 1065, 1065, 1065,
 /*    10 */  1065, 1065, 1065,  921, 1065, 1065, 1065, 1065, 1065, 1065,
 /*    20 */  1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065,
 /*    30 */  1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065,
 /*    40 */  1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065, 1065,
 /*    50 */  1065, 1065, 1065, 1065,  946,  374,  353,  195,  527,  679,
 /*    60 */   829,  829,  829,   62,   62,  451,  539,  604,  829,  829,
 /*    70 */   829,  829,  829,  829,  829,  829,  829,  829,   60,  562,
 /*    80 */   562,  562,  562,  562, -121, -121,  -26,  370,  289,  212,
 /*    90 */   137,  528,  528,  528,  528,  528,  528,  528,  528,  528,
 /*   100 */   528,  528,  528,  528,  528,  678,  603,  528,  528,  753,
 /*   110 */   828,  828,  828,  -66,  -66,  -66,  -66,  -66,  -66,   22,
 /*   120 */   160,   77,  -30,   86,  665,  515,  -89,  945,  873,   84,
 /*   130 */   829,  829,  829,  325,  460,  325,  375,  829,  829,  371,
 /*   140 */   421,  352,  267,  536,  536,  -20,  -55,  340,   81,  836,
 /*   150 */   683,  810,  816,  836,  763,  834,  763,  832,  832,  735,
 /*   160 */   731,  732,  725,  721,  707,  674,  701,  701,  701,  701,
 /*   170 */   683,  673,  673,  673, -121, -121,   32, -118,   61,  688,
 /*   180 */   620,  470,  455,  313,  226,  211,  164,  150,   33,  525,
 /*   190 */   468,  378,  324,  280,  272,  100,  112,  200,  259,  146,
 /*   200 */   133,  108,    6,  192,   -7,  712,  692,  610,  601,  726,
 /*   210 */   619,  618,  573,  588,  532,  590,  523,  521,  564,  567,
 /*   220 */   566,  489,  435,  487,  472,  438,  434,  411,  410,  385,
 /*   230 */   348,  287,  295,  276,  243,  213,  250,  297,  136,  124,
 /*   240 */   122,   49,   23,   47,  -90,  -29, -120,   13,    2,
};
#define YY_REDUCE_USE_DFLT (-170)
#define YY_REDUCE_COUNT (175)
#define YY_REDUCE_MIN   (-169)
#define YY_REDUCE_MAX   (1190)
static const short yy_reduce_ofst[] = {
 /*     0 */   715,  148,  218,  -80,  311,  308,  229,  466,  461,  384,
 /*    10 */   233,   70, -168, 1190, 1181, 1174, 1162, 1155, 1146, 1141,
 /*    20 */  1137, 1134, 1120, 1101, 1098, 1087, 1083, 1080, 1076, 1073,
 /*    30 */  1061, 1039, 1026, 1023, 1019, 1004, 1001,  979,  976,  951,
 /*    40 */   939,  904,  882,  782,  778,  691,  687,  684,  616,  613,
 /*    50 */   609,  541,  538,  534,  167,   15,  333,  758,  -71, -169,
 /*    60 */   236,  221,   97,  606,  563,  592,  296,  496,  556,  545,
 /*    70 */   535,  483,  401,  387,  341,  480,  391,  336,  617,  557,
 /*    80 */   551,  478,  474,  407,  161,  120,  638,  638,  638,  638,
 /*    90 */   638,  638,  638,  638,  638,  638,  638,  638,  638,  638,
 /*   100 */   638,  638,  638,  638,  638,  638,  638,  638,  638,  638,
 /*   110 */   638,  638,  638,  638,  638,  638,  638,  638,  638,  638,
 /*   120 */   638,  638,  780,  638,  821,  819,  638,  791,  818,  813,
 /*   130 */   815,  812,  811,  807,  804,  805,  802,  791,  785,  776,
 /*   140 */   774,  760,  749,  638,  638,  740,  765,  757,  773,  762,
 /*   150 */   755,  752,  743,  713,  728,  722,  719,  716,  714,  681,
 /*   160 */   672,  685,  677,  669,  659,  614,  638,  638,  638,  638,
 /*   170 */   631,  626,  625,  623,  596,  591,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */   567,  500,  500,  500,  567,  567,  567,  500,  500,  500,
 /*    10 */   567,  567,  541,  567,  567,  567,  567,  567,  567,  567,
 /*    20 */   567,  567,  567,  567,  567,  567,  567,  567,  567,  567,
 /*    30 */   567,  567,  567,  567,  567,  567,  567,  567,  567,  567,
 /*    40 */   567,  567,  567,  567,  567,  567,  567,  567,  567,  567,
 /*    50 */   567,  567,  567,  567,  409,  567,  409,  567,  409,  567,
 /*    60 */   567,  567,  567,  455,  455,  493,  373,  409,  567,  567,
 /*    70 */   567,  567,  409,  409,  409,  567,  567,  567,  567,  567,
 /*    80 */   567,  567,  567,  567,  465,  485,  446,  567,  567,  567,
 /*    90 */   567,  437,  436,  497,  467,  499,  498,  457,  448,  447,
 /*   100 */   543,  544,  542,  540,  502,  567,  567,  501,  434,  519,
 /*   110 */   530,  529,  518,  533,  526,  525,  524,  523,  522,  528,
 /*   120 */   521,  527,  461,  515,  567,  567,  512,  567,  567,  567,
 /*   130 */   567,  567,  567,  567,  403,  567,  403,  567,  567,  433,
 /*   140 */   379,  493,  493,  513,  514,  545,  460,  494,  567,  424,
 /*   150 */   433,  421,  428,  424,  401,  389,  401,  567,  567,  464,
 /*   160 */   468,  445,  449,  456,  458,  567,  531,  517,  516,  520,
 /*   170 */   433,  442,  442,  442,  555,  555,  567,  567,  567,  567,
 /*   180 */   567,  567,  567,  567,  534,  567,  567,  423,  567,  567,
 /*   190 */   567,  567,  567,  394,  393,  567,  567,  567,  567,  567,
 /*   200 */   567,  567,  567,  567,  567,  567,  567,  567,  567,  567,
 /*   210 */   567,  567,  422,  567,  567,  567,  567,  387,  567,  567,
 /*   220 */   567,  567,  496,  567,  567,  567,  567,  567,  567,  567,
 /*   230 */   459,  567,  450,  567,  567,  567,  567,  567,  567,  567,
 /*   240 */   567,  565,  564,  506,  504,  565,  564,  567,  567,  435,
 /*   250 */   432,  425,  429,  427,  426,  418,  417,  416,  420,  419,
 /*   260 */   392,  391,  396,  395,  400,  399,  398,  397,  390,  388,
 /*   270 */   386,  385,  402,  384,  383,  382,  376,  375,  410,  408,
 /*   280 */   407,  406,  405,  380,  404,  415,  414,  413,  412,  411,
 /*   290 */   381,  374,  369,  439,  443,  495,  487,  486,  484,  483,
 /*   300 */   482,  492,  491,  481,  480,  431,  463,  430,  462,  479,
 /*   310 */   478,  477,  476,  475,  474,  473,  472,  471,  470,  469,
 /*   320 */   466,  452,  454,  453,  451,  444,  534,  509,  507,  537,
 /*   330 */   538,  547,  554,  552,  551,  550,  549,  548,  539,  546,
 /*   340 */   535,  536,  532,  510,  490,  489,  488,  508,  505,  559,
 /*   350 */   558,  557,  556,  553,  503,  566,  563,  562,  561,  560,
 /*   360 */   511,  441,  440,  438,  370,  372,  371,
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
    0,  /*    TEXTUAL => nothing */
   67,  /*     STRING => TEXTUAL */
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
  priv_gda_sql_parserARG_SDECL                /* A place to hold %extra_argument */
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
void priv_gda_sql_parserTrace(FILE *TraceFILE, char *zTracePrompt){
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
  "WAIT",          "NOWAIT",        "BATCH",         "TEXTUAL",     
  "STRING",        "OR",            "AND",           "NOT",         
  "IS",            "NOTLIKE",       "NOTILIKE",      "IN",          
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
  "SEMI",          "END_OF_FILE",   "TRANSACTION",   "COMMA",       
  "INTEGER",       "TO",            "INSERT",        "INTO",        
  "VALUES",        "DELETE",        "FROM",          "WHERE",       
  "UPDATE",        "SET",           "ALL",           "SELECT",      
  "LIMIT",         "ORDER",         "BY",            "HAVING",      
  "GROUP",         "USING",         "ON",            "OUTER",       
  "DOT",           "AS",            "DISTINCT",      "CASE",        
  "WHEN",          "THEN",          "ELSE",          "NULL",        
  "FLOAT",         "UNSPECVAL",     "LSBRACKET",     "RSBRACKET",   
  "SIMPLEPARAM",   "PNAME",         "PDESCR",        "PTYPE",       
  "PNULLOK",       "error",         "stmt",          "cmd",         
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
 /* 116 */ "seltarget ::= LP compound RP ID",
 /* 117 */ "sclp ::= selcollist COMMA",
 /* 118 */ "sclp ::=",
 /* 119 */ "selcollist ::= sclp expr as",
 /* 120 */ "selcollist ::= sclp starname",
 /* 121 */ "starname ::= STAR",
 /* 122 */ "starname ::= nm DOT STAR",
 /* 123 */ "starname ::= nm DOT nm DOT STAR",
 /* 124 */ "as ::= AS fullname",
 /* 125 */ "as ::= AS value",
 /* 126 */ "as ::=",
 /* 127 */ "distinct ::=",
 /* 128 */ "distinct ::= ALL",
 /* 129 */ "distinct ::= DISTINCT",
 /* 130 */ "distinct ::= DISTINCT ON expr",
 /* 131 */ "rnexprlist ::= rnexprlist COMMA expr",
 /* 132 */ "rnexprlist ::= expr",
 /* 133 */ "rexprlist ::=",
 /* 134 */ "rexprlist ::= rexprlist COMMA expr",
 /* 135 */ "rexprlist ::= expr",
 /* 136 */ "expr ::= pvalue",
 /* 137 */ "expr ::= value",
 /* 138 */ "expr ::= LP expr RP",
 /* 139 */ "expr ::= fullname",
 /* 140 */ "expr ::= fullname LP rexprlist RP",
 /* 141 */ "expr ::= fullname LP compound RP",
 /* 142 */ "expr ::= fullname LP starname RP",
 /* 143 */ "expr ::= CAST LP expr AS fullname RP",
 /* 144 */ "expr ::= expr PGCAST fullname",
 /* 145 */ "expr ::= expr PLUS|MINUS expr",
 /* 146 */ "expr ::= expr STAR expr",
 /* 147 */ "expr ::= expr SLASH|REM expr",
 /* 148 */ "expr ::= expr BITAND|BITOR expr",
 /* 149 */ "expr ::= MINUS expr",
 /* 150 */ "expr ::= PLUS expr",
 /* 151 */ "expr ::= expr AND expr",
 /* 152 */ "expr ::= expr OR expr",
 /* 153 */ "expr ::= expr CONCAT expr",
 /* 154 */ "expr ::= expr GT|LEQ|GEQ|LT expr",
 /* 155 */ "expr ::= expr DIFF|EQ expr",
 /* 156 */ "expr ::= expr LIKE expr",
 /* 157 */ "expr ::= expr ILIKE expr",
 /* 158 */ "expr ::= expr NOTLIKE expr",
 /* 159 */ "expr ::= expr NOTILIKE expr",
 /* 160 */ "expr ::= expr REGEXP|REGEXP_CI|NOT_REGEXP|NOT_REGEXP_CI|SIMILAR expr",
 /* 161 */ "expr ::= expr BETWEEN expr AND expr",
 /* 162 */ "expr ::= expr NOT BETWEEN expr AND expr",
 /* 163 */ "expr ::= NOT expr",
 /* 164 */ "expr ::= BITNOT expr",
 /* 165 */ "expr ::= expr uni_op",
 /* 166 */ "expr ::= expr IS expr",
 /* 167 */ "expr ::= LP compound RP",
 /* 168 */ "expr ::= expr IN LP rexprlist RP",
 /* 169 */ "expr ::= expr IN LP compound RP",
 /* 170 */ "expr ::= expr NOT IN LP rexprlist RP",
 /* 171 */ "expr ::= expr NOT IN LP compound RP",
 /* 172 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 173 */ "case_operand ::= expr",
 /* 174 */ "case_operand ::=",
 /* 175 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 176 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 177 */ "case_else ::= ELSE expr",
 /* 178 */ "case_else ::=",
 /* 179 */ "uni_op ::= ISNULL",
 /* 180 */ "uni_op ::= IS NOTNULL",
 /* 181 */ "value ::= NULL",
 /* 182 */ "value ::= STRING",
 /* 183 */ "value ::= INTEGER",
 /* 184 */ "value ::= FLOAT",
 /* 185 */ "pvalue ::= UNSPECVAL LSBRACKET paramspec RSBRACKET",
 /* 186 */ "pvalue ::= value LSBRACKET paramspec RSBRACKET",
 /* 187 */ "pvalue ::= SIMPLEPARAM",
 /* 188 */ "paramspec ::=",
 /* 189 */ "paramspec ::= paramspec PNAME",
 /* 190 */ "paramspec ::= paramspec PDESCR",
 /* 191 */ "paramspec ::= paramspec PTYPE",
 /* 192 */ "paramspec ::= paramspec PNULLOK",
 /* 193 */ "nm ::= JOIN",
 /* 194 */ "nm ::= ID",
 /* 195 */ "nm ::= TEXTUAL",
 /* 196 */ "nm ::= LIMIT",
 /* 197 */ "fullname ::= nm",
 /* 198 */ "fullname ::= nm DOT nm",
 /* 199 */ "fullname ::= nm DOT nm DOT nm",
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
** to priv_gda_sql_parser and priv_gda_sql_parserFree.
*/
void *priv_gda_sql_parserAlloc(void *(*mallocProc)(size_t)){
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
  priv_gda_sql_parserARG_FETCH;
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
    case 67: /* TEXTUAL */
    case 68: /* STRING */
    case 69: /* OR */
    case 70: /* AND */
    case 71: /* NOT */
    case 72: /* IS */
    case 73: /* NOTLIKE */
    case 74: /* NOTILIKE */
    case 75: /* IN */
    case 76: /* ISNULL */
    case 77: /* NOTNULL */
    case 78: /* DIFF */
    case 79: /* EQ */
    case 80: /* BETWEEN */
    case 81: /* GT */
    case 82: /* LEQ */
    case 83: /* LT */
    case 84: /* GEQ */
    case 85: /* REGEXP */
    case 86: /* REGEXP_CI */
    case 87: /* NOT_REGEXP */
    case 88: /* NOT_REGEXP_CI */
    case 89: /* SIMILAR */
    case 90: /* ESCAPE */
    case 91: /* BITAND */
    case 92: /* BITOR */
    case 93: /* LSHIFT */
    case 94: /* RSHIFT */
    case 95: /* PLUS */
    case 96: /* MINUS */
    case 97: /* STAR */
    case 98: /* SLASH */
    case 99: /* REM */
    case 100: /* CONCAT */
    case 101: /* COLLATE */
    case 102: /* UMINUS */
    case 103: /* UPLUS */
    case 104: /* BITNOT */
    case 105: /* LP */
    case 106: /* RP */
    case 107: /* JOIN */
    case 108: /* INNER */
    case 109: /* NATURAL */
    case 110: /* LEFT */
    case 111: /* RIGHT */
    case 112: /* FULL */
    case 113: /* CROSS */
    case 114: /* UNION */
    case 115: /* EXCEPT */
    case 116: /* INTERSECT */
    case 117: /* PGCAST */
    case 118: /* ILLEGAL */
    case 119: /* SQLCOMMENT */
    case 120: /* SEMI */
    case 121: /* END_OF_FILE */
    case 122: /* TRANSACTION */
    case 123: /* COMMA */
    case 124: /* INTEGER */
    case 125: /* TO */
    case 126: /* INSERT */
    case 127: /* INTO */
    case 128: /* VALUES */
    case 129: /* DELETE */
    case 130: /* FROM */
    case 131: /* WHERE */
    case 132: /* UPDATE */
    case 133: /* SET */
    case 134: /* ALL */
    case 135: /* SELECT */
    case 136: /* LIMIT */
    case 137: /* ORDER */
    case 138: /* BY */
    case 139: /* HAVING */
    case 140: /* GROUP */
    case 141: /* USING */
    case 142: /* ON */
    case 143: /* OUTER */
    case 144: /* DOT */
    case 145: /* AS */
    case 146: /* DISTINCT */
    case 147: /* CASE */
    case 148: /* WHEN */
    case 149: /* THEN */
    case 150: /* ELSE */
    case 151: /* NULL */
    case 152: /* FLOAT */
    case 153: /* UNSPECVAL */
    case 154: /* LSBRACKET */
    case 155: /* RSBRACKET */
    case 156: /* SIMPLEPARAM */
    case 157: /* PNAME */
    case 158: /* PDESCR */
    case 159: /* PTYPE */
    case 160: /* PNULLOK */
{
#line 9 "./parser.y"
if ((yypminor->yy0)) {
#ifdef GDA_DEBUG_NO
		 gchar *str = gda_sql_value_stringify ((yypminor->yy0));
		 g_print ("___ token destructor /%s/\n", str)
		 g_free (str);
#endif
		 g_value_unset ((yypminor->yy0)); g_free ((yypminor->yy0));}
#line 1406 "parser.c"
}
      break;
    case 162: /* stmt */
{
#line 280 "./parser.y"
g_print ("Statement destroyed by parser: %p\n", (yypminor->yy116)); gda_sql_statement_free ((yypminor->yy116));
#line 1413 "parser.c"
}
      break;
    case 163: /* cmd */
    case 165: /* compound */
    case 182: /* selectcmd */
{
#line 303 "./parser.y"
gda_sql_statement_free ((yypminor->yy116));
#line 1422 "parser.c"
}
      break;
    case 175: /* inscollist_opt */
    case 178: /* inscollist */
    case 198: /* using_opt */
{
#line 480 "./parser.y"
if ((yypminor->yy333)) {g_slist_foreach ((yypminor->yy333), (GFunc) gda_sql_field_free, NULL); g_slist_free ((yypminor->yy333));}
#line 1431 "parser.c"
}
      break;
    case 176: /* rexprlist */
    case 193: /* rnexprlist */
{
#line 769 "./parser.y"
if ((yypminor->yy325)) {g_slist_foreach ((yypminor->yy325), (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy325));}
#line 1439 "parser.c"
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

#line 1452 "parser.c"
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
#line 1463 "parser.c"
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

#line 1478 "parser.c"
}
      break;
    case 184: /* distinct */
{
#line 755 "./parser.y"
if ((yypminor->yy297)) {if ((yypminor->yy297)->expr) gda_sql_expr_free ((yypminor->yy297)->expr); g_free ((yypminor->yy297));}
#line 1485 "parser.c"
}
      break;
    case 185: /* selcollist */
    case 201: /* sclp */
{
#line 712 "./parser.y"
g_slist_foreach ((yypminor->yy325), (GFunc) gda_sql_select_field_free, NULL); g_slist_free ((yypminor->yy325));
#line 1493 "parser.c"
}
      break;
    case 186: /* from */
    case 194: /* seltablist */
    case 195: /* stl_prefix */
{
#line 635 "./parser.y"
gda_sql_select_from_free ((yypminor->yy191));
#line 1502 "parser.c"
}
      break;
    case 187: /* groupby_opt */
{
#line 630 "./parser.y"
if ((yypminor->yy333)) {g_slist_foreach ((yypminor->yy333), (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy333));}
#line 1509 "parser.c"
}
      break;
    case 189: /* orderby_opt */
    case 191: /* sortlist */
{
#line 599 "./parser.y"
if ((yypminor->yy325)) {g_slist_foreach ((yypminor->yy325), (GFunc) gda_sql_select_order_free, NULL); g_slist_free ((yypminor->yy325));}
#line 1517 "parser.c"
}
      break;
    case 190: /* limit_opt */
{
#line 592 "./parser.y"
gda_sql_expr_free ((yypminor->yy44).count); gda_sql_expr_free ((yypminor->yy44).offset);
#line 1524 "parser.c"
}
      break;
    case 196: /* seltarget */
{
#line 694 "./parser.y"
gda_sql_select_target_free ((yypminor->yy134));
#line 1531 "parser.c"
}
      break;
    case 206: /* case_operand */
    case 208: /* case_else */
{
#line 936 "./parser.y"
gda_sql_expr_free ((yypminor->yy146));
#line 1539 "parser.c"
}
      break;
    case 207: /* case_exprlist */
{
#line 941 "./parser.y"
g_slist_foreach ((yypminor->yy59).when_list, (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy59).when_list);
	g_slist_foreach ((yypminor->yy59).then_list, (GFunc) gda_sql_expr_free, NULL); g_slist_free ((yypminor->yy59).then_list);
#line 1547 "parser.c"
}
      break;
    case 209: /* paramspec */
{
#line 979 "./parser.y"
gda_sql_param_spec_free ((yypminor->yy339));
#line 1554 "parser.c"
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
**       obtained from priv_gda_sql_parserAlloc.
** <li>  A pointer to a function used to reclaim memory obtained
**       from malloc.
** </ul>
*/
void priv_gda_sql_parserFree(
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
int priv_gda_sql_parserStackPeak(void *p){
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
   priv_gda_sql_parserARG_FETCH;
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
#line 1740 "parser.c"
   priv_gda_sql_parserARG_STORE; /* Suppress warning about unused %extra_argument var */
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
  priv_gda_sql_parserARG_FETCH;
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
#line 2054 "parser.c"
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
#line 2072 "parser.c"
        break;
      case 2: /* cmd ::= LP cmd RP */
      case 3: /* compound ::= LP compound RP */ yytestcase(yyruleno==3);
#line 296 "./parser.y"
{yygotominor.yy116 = yymsp[-1].minor.yy116;  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 2080 "parser.c"
        break;
      case 4: /* eos ::= SEMI */
#line 299 "./parser.y"
{
  yy_destructor(yypParser,120,&yymsp[0].minor);
}
#line 2087 "parser.c"
        break;
      case 5: /* eos ::= END_OF_FILE */
#line 300 "./parser.y"
{
  yy_destructor(yypParser,121,&yymsp[0].minor);
}
#line 2094 "parser.c"
        break;
      case 6: /* cmd ::= BEGIN */
#line 308 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);  yy_destructor(yypParser,8,&yymsp[0].minor);
}
#line 2100 "parser.c"
        break;
      case 7: /* cmd ::= BEGIN TRANSACTION nm_opt */
#line 309 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					 gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2109 "parser.c"
        break;
      case 8: /* cmd ::= BEGIN transtype TRANSACTION nm_opt */
#line 313 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						      gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-2].minor.yy0);
						      gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2119 "parser.c"
        break;
      case 9: /* cmd ::= BEGIN transtype nm_opt */
#line 318 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					  gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-1].minor.yy0);
					  gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
}
#line 2128 "parser.c"
        break;
      case 10: /* cmd ::= BEGIN transilev */
#line 323 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
				gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-1].minor);
}
#line 2136 "parser.c"
        break;
      case 11: /* cmd ::= BEGIN TRANSACTION transilev */
#line 327 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					    gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2145 "parser.c"
        break;
      case 12: /* cmd ::= BEGIN TRANSACTION transtype */
#line 331 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
					    gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,8,&yymsp[-2].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2154 "parser.c"
        break;
      case 13: /* cmd ::= BEGIN TRANSACTION transtype opt_comma transilev */
#line 335 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
								   gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-2].minor.yy0);
								   gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-4].minor);
  yy_destructor(yypParser,122,&yymsp[-3].minor);
}
#line 2164 "parser.c"
        break;
      case 14: /* cmd ::= BEGIN TRANSACTION transilev opt_comma transtype */
#line 340 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
								   gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[0].minor.yy0);
								   gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[-2].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-4].minor);
  yy_destructor(yypParser,122,&yymsp[-3].minor);
}
#line 2174 "parser.c"
        break;
      case 15: /* cmd ::= BEGIN transtype opt_comma transilev */
#line 345 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						       gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[-2].minor.yy0);
						       gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[0].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
}
#line 2183 "parser.c"
        break;
      case 16: /* cmd ::= BEGIN transilev opt_comma transtype */
#line 350 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_BEGIN);
						       gda_sql_statement_trans_take_mode (yygotominor.yy116, yymsp[0].minor.yy0);
						       gda_sql_statement_trans_set_isol_level (yygotominor.yy116, yymsp[-2].minor.yy169);
  yy_destructor(yypParser,8,&yymsp[-3].minor);
}
#line 2192 "parser.c"
        break;
      case 17: /* cmd ::= END trans_opt_kw nm_opt */
#line 355 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
					gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,17,&yymsp[-2].minor);
}
#line 2200 "parser.c"
        break;
      case 18: /* cmd ::= COMMIT nm_opt */
#line 359 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
			      gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-1].minor);
}
#line 2208 "parser.c"
        break;
      case 19: /* cmd ::= COMMIT TRANSACTION nm_opt */
#line 363 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);
					  gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,49,&yymsp[-2].minor);
  yy_destructor(yypParser,122,&yymsp[-1].minor);
}
#line 2217 "parser.c"
        break;
      case 20: /* cmd ::= COMMIT FORCE STRING */
#line 367 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-2].minor);
  yy_destructor(yypParser,63,&yymsp[-1].minor);
  yy_destructor(yypParser,68,&yymsp[0].minor);
}
#line 2225 "parser.c"
        break;
      case 21: /* cmd ::= COMMIT FORCE STRING COMMA INTEGER */
#line 368 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-4].minor);
  yy_destructor(yypParser,63,&yymsp[-3].minor);
  yy_destructor(yypParser,68,&yymsp[-2].minor);
  yy_destructor(yypParser,123,&yymsp[-1].minor);
  yy_destructor(yypParser,124,&yymsp[0].minor);
}
#line 2235 "parser.c"
        break;
      case 22: /* cmd ::= COMMIT COMMENT STRING */
#line 369 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-2].minor);
  yy_destructor(yypParser,62,&yymsp[-1].minor);
  yy_destructor(yypParser,68,&yymsp[0].minor);
}
#line 2243 "parser.c"
        break;
      case 23: /* cmd ::= COMMIT COMMENT STRING ora_commit_write */
#line 370 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-3].minor);
  yy_destructor(yypParser,62,&yymsp[-2].minor);
  yy_destructor(yypParser,68,&yymsp[-1].minor);
}
#line 2251 "parser.c"
        break;
      case 24: /* cmd ::= COMMIT ora_commit_write */
#line 371 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMMIT);  yy_destructor(yypParser,49,&yymsp[-1].minor);
}
#line 2257 "parser.c"
        break;
      case 25: /* cmd ::= ROLLBACK trans_opt_kw nm_opt */
#line 373 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK);
					     gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,50,&yymsp[-2].minor);
}
#line 2265 "parser.c"
        break;
      case 26: /* ora_commit_write ::= WRITE IMMEDIATE */
#line 377 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,23,&yymsp[0].minor);
}
#line 2273 "parser.c"
        break;
      case 27: /* ora_commit_write ::= WRITE BATCH */
#line 378 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,66,&yymsp[0].minor);
}
#line 2281 "parser.c"
        break;
      case 28: /* ora_commit_write ::= WRITE WAIT */
#line 379 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2289 "parser.c"
        break;
      case 29: /* ora_commit_write ::= WRITE NOWAIT */
#line 380 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2297 "parser.c"
        break;
      case 30: /* ora_commit_write ::= WRITE IMMEDIATE WAIT */
#line 381 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,23,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2306 "parser.c"
        break;
      case 31: /* ora_commit_write ::= WRITE IMMEDIATE NOWAIT */
#line 382 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,23,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2315 "parser.c"
        break;
      case 32: /* ora_commit_write ::= WRITE BATCH WAIT */
#line 383 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,66,&yymsp[-1].minor);
  yy_destructor(yypParser,64,&yymsp[0].minor);
}
#line 2324 "parser.c"
        break;
      case 33: /* ora_commit_write ::= WRITE BATCH NOWAIT */
#line 384 "./parser.y"
{
  yy_destructor(yypParser,58,&yymsp[-2].minor);
  yy_destructor(yypParser,66,&yymsp[-1].minor);
  yy_destructor(yypParser,65,&yymsp[0].minor);
}
#line 2333 "parser.c"
        break;
      case 35: /* trans_opt_kw ::= TRANSACTION */
#line 387 "./parser.y"
{
  yy_destructor(yypParser,122,&yymsp[0].minor);
}
#line 2340 "parser.c"
        break;
      case 37: /* opt_comma ::= COMMA */
#line 390 "./parser.y"
{
  yy_destructor(yypParser,123,&yymsp[0].minor);
}
#line 2347 "parser.c"
        break;
      case 38: /* transilev ::= ISOLATION LEVEL SERIALIZABLE */
#line 393 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_SERIALIZABLE;  yy_destructor(yypParser,51,&yymsp[-2].minor);
  yy_destructor(yypParser,52,&yymsp[-1].minor);
  yy_destructor(yypParser,53,&yymsp[0].minor);
}
#line 2355 "parser.c"
        break;
      case 39: /* transilev ::= ISOLATION LEVEL REPEATABLE READ */
#line 394 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_REPEATABLE_READ;  yy_destructor(yypParser,51,&yymsp[-3].minor);
  yy_destructor(yypParser,52,&yymsp[-2].minor);
  yy_destructor(yypParser,57,&yymsp[-1].minor);
  yy_destructor(yypParser,54,&yymsp[0].minor);
}
#line 2364 "parser.c"
        break;
      case 40: /* transilev ::= ISOLATION LEVEL READ COMMITTED */
#line 395 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_READ_COMMITTED;  yy_destructor(yypParser,51,&yymsp[-3].minor);
  yy_destructor(yypParser,52,&yymsp[-2].minor);
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,55,&yymsp[0].minor);
}
#line 2373 "parser.c"
        break;
      case 41: /* transilev ::= ISOLATION LEVEL READ UNCOMMITTED */
#line 396 "./parser.y"
{yygotominor.yy169 = GDA_TRANSACTION_ISOLATION_READ_UNCOMMITTED;  yy_destructor(yypParser,51,&yymsp[-3].minor);
  yy_destructor(yypParser,52,&yymsp[-2].minor);
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,56,&yymsp[0].minor);
}
#line 2382 "parser.c"
        break;
      case 42: /* nm_opt ::= */
      case 57: /* opt_on_conflict ::= */ yytestcase(yyruleno==57);
      case 126: /* as ::= */ yytestcase(yyruleno==126);
#line 398 "./parser.y"
{yygotominor.yy0 = NULL;}
#line 2389 "parser.c"
        break;
      case 43: /* nm_opt ::= nm */
      case 44: /* transtype ::= DEFERRED */ yytestcase(yyruleno==44);
      case 45: /* transtype ::= IMMEDIATE */ yytestcase(yyruleno==45);
      case 46: /* transtype ::= EXCLUSIVE */ yytestcase(yyruleno==46);
      case 121: /* starname ::= STAR */ yytestcase(yyruleno==121);
      case 182: /* value ::= STRING */ yytestcase(yyruleno==182);
      case 183: /* value ::= INTEGER */ yytestcase(yyruleno==183);
      case 184: /* value ::= FLOAT */ yytestcase(yyruleno==184);
      case 193: /* nm ::= JOIN */ yytestcase(yyruleno==193);
      case 194: /* nm ::= ID */ yytestcase(yyruleno==194);
      case 195: /* nm ::= TEXTUAL */ yytestcase(yyruleno==195);
      case 196: /* nm ::= LIMIT */ yytestcase(yyruleno==196);
      case 197: /* fullname ::= nm */ yytestcase(yyruleno==197);
#line 399 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;}
#line 2406 "parser.c"
        break;
      case 47: /* transtype ::= READ WRITE */
#line 404 "./parser.y"
{yygotominor.yy0 = g_new0 (GValue, 1);
			      g_value_init (yygotominor.yy0, G_TYPE_STRING);
			      g_value_set_string (yygotominor.yy0, "READ_WRITE");
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,58,&yymsp[0].minor);
}
#line 2416 "parser.c"
        break;
      case 48: /* transtype ::= READ ONLY */
#line 408 "./parser.y"
{yygotominor.yy0 = g_new0 (GValue, 1);
			     g_value_init (yygotominor.yy0, G_TYPE_STRING);
			     g_value_set_string (yygotominor.yy0, "READ_ONLY");
  yy_destructor(yypParser,54,&yymsp[-1].minor);
  yy_destructor(yypParser,59,&yymsp[0].minor);
}
#line 2426 "parser.c"
        break;
      case 49: /* cmd ::= SAVEPOINT nm */
#line 416 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_SAVEPOINT);
				    gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2434 "parser.c"
        break;
      case 50: /* cmd ::= RELEASE SAVEPOINT nm */
#line 420 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE_SAVEPOINT);
				     gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,61,&yymsp[-2].minor);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2443 "parser.c"
        break;
      case 51: /* cmd ::= RELEASE nm */
#line 424 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE_SAVEPOINT);
			   gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,61,&yymsp[-1].minor);
}
#line 2451 "parser.c"
        break;
      case 52: /* cmd ::= ROLLBACK trans_opt_kw TO nm */
#line 428 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT);
					    gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,50,&yymsp[-3].minor);
  yy_destructor(yypParser,125,&yymsp[-1].minor);
}
#line 2460 "parser.c"
        break;
      case 53: /* cmd ::= ROLLBACK trans_opt_kw TO SAVEPOINT nm */
#line 432 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_ROLLBACK_SAVEPOINT);
						      gda_sql_statement_trans_take_name (yygotominor.yy116, yymsp[0].minor.yy0);
  yy_destructor(yypParser,50,&yymsp[-4].minor);
  yy_destructor(yypParser,125,&yymsp[-2].minor);
  yy_destructor(yypParser,60,&yymsp[-1].minor);
}
#line 2470 "parser.c"
        break;
      case 54: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt VALUES LP rexprlist RP */
#line 439 "./parser.y"
{
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
	gda_sql_statement_insert_take_table_name (yygotominor.yy116, yymsp[-5].minor.yy0);
	gda_sql_statement_insert_take_fields_list (yygotominor.yy116, yymsp[-4].minor.yy333);
	gda_sql_statement_insert_take_1_values_list (yygotominor.yy116, g_slist_reverse (yymsp[-1].minor.yy325));
	gda_sql_statement_insert_take_on_conflict (yygotominor.yy116, yymsp[-7].minor.yy0);
  yy_destructor(yypParser,126,&yymsp[-8].minor);
  yy_destructor(yypParser,127,&yymsp[-6].minor);
  yy_destructor(yypParser,128,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 2486 "parser.c"
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
  yy_destructor(yypParser,126,&yymsp[-9].minor);
  yy_destructor(yypParser,127,&yymsp[-7].minor);
  yy_destructor(yypParser,128,&yymsp[-4].minor);
  yy_destructor(yypParser,105,&yymsp[-3].minor);
  yy_destructor(yypParser,106,&yymsp[-1].minor);
}
#line 2503 "parser.c"
        break;
      case 56: /* cmd ::= INSERT opt_on_conflict INTO fullname inscollist_opt compound */
#line 456 "./parser.y"
{
        yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_INSERT);
        gda_sql_statement_insert_take_table_name (yygotominor.yy116, yymsp[-2].minor.yy0);
        gda_sql_statement_insert_take_fields_list (yygotominor.yy116, yymsp[-1].minor.yy333);
        gda_sql_statement_insert_take_select (yygotominor.yy116, yymsp[0].minor.yy116);
        gda_sql_statement_insert_take_on_conflict (yygotominor.yy116, yymsp[-4].minor.yy0);
  yy_destructor(yypParser,126,&yymsp[-5].minor);
  yy_destructor(yypParser,127,&yymsp[-3].minor);
}
#line 2516 "parser.c"
        break;
      case 58: /* opt_on_conflict ::= OR ID */
#line 466 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;  yy_destructor(yypParser,69,&yymsp[-1].minor);
}
#line 2522 "parser.c"
        break;
      case 59: /* ins_extra_values ::= ins_extra_values COMMA LP rexprlist RP */
#line 476 "./parser.y"
{yygotominor.yy333 = g_slist_append (yymsp[-4].minor.yy333, g_slist_reverse (yymsp[-1].minor.yy325));  yy_destructor(yypParser,123,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 2530 "parser.c"
        break;
      case 60: /* ins_extra_values ::= COMMA LP rexprlist RP */
#line 477 "./parser.y"
{yygotominor.yy333 = g_slist_append (NULL, g_slist_reverse (yymsp[-1].minor.yy325));  yy_destructor(yypParser,123,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 2538 "parser.c"
        break;
      case 61: /* inscollist_opt ::= */
      case 97: /* using_opt ::= */ yytestcase(yyruleno==97);
#line 481 "./parser.y"
{yygotominor.yy333 = NULL;}
#line 2544 "parser.c"
        break;
      case 62: /* inscollist_opt ::= LP inscollist RP */
#line 482 "./parser.y"
{yygotominor.yy333 = yymsp[-1].minor.yy333;  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 2551 "parser.c"
        break;
      case 63: /* inscollist ::= inscollist COMMA fullname */
#line 486 "./parser.y"
{GdaSqlField *field;
						    field = gda_sql_field_new (NULL);
						    gda_sql_field_take_name (field, yymsp[0].minor.yy0);
						    yygotominor.yy333 = g_slist_append (yymsp[-2].minor.yy333, field);
  yy_destructor(yypParser,123,&yymsp[-1].minor);
}
#line 2561 "parser.c"
        break;
      case 64: /* inscollist ::= fullname */
#line 491 "./parser.y"
{GdaSqlField *field = gda_sql_field_new (NULL);
				gda_sql_field_take_name (field, yymsp[0].minor.yy0);
				yygotominor.yy333 = g_slist_prepend (NULL, field);
}
#line 2569 "parser.c"
        break;
      case 65: /* cmd ::= DELETE FROM fullname where_opt */
#line 497 "./parser.y"
{yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_DELETE); 
						  gda_sql_statement_delete_take_table_name (yygotominor.yy116, yymsp[-1].minor.yy0);
						  gda_sql_statement_delete_take_condition (yygotominor.yy116, yymsp[0].minor.yy70);  yy_destructor(yypParser,129,&yymsp[-3].minor);
  yy_destructor(yypParser,130,&yymsp[-2].minor);
}
#line 2578 "parser.c"
        break;
      case 66: /* where_opt ::= */
      case 89: /* having_opt ::= */ yytestcase(yyruleno==89);
      case 101: /* on_cond ::= */ yytestcase(yyruleno==101);
#line 503 "./parser.y"
{yygotominor.yy70 = NULL;}
#line 2585 "parser.c"
        break;
      case 67: /* where_opt ::= WHERE expr */
#line 504 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;  yy_destructor(yypParser,131,&yymsp[-1].minor);
}
#line 2591 "parser.c"
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
  yy_destructor(yypParser,132,&yymsp[-5].minor);
  yy_destructor(yypParser,133,&yymsp[-2].minor);
}
#line 2610 "parser.c"
        break;
      case 69: /* setlist ::= setlist COMMA fullname EQ expr */
#line 531 "./parser.y"
{UpdateSet *set;
							 set = g_new (UpdateSet, 1);
							 set->fname = yymsp[-2].minor.yy0;
							 set->expr = yymsp[0].minor.yy70;
							 yygotominor.yy333 = g_slist_append (yymsp[-4].minor.yy333, set);
  yy_destructor(yypParser,123,&yymsp[-3].minor);
  yy_destructor(yypParser,79,&yymsp[-1].minor);
}
#line 2622 "parser.c"
        break;
      case 70: /* setlist ::= fullname EQ expr */
#line 537 "./parser.y"
{UpdateSet *set;
					set = g_new (UpdateSet, 1);
					set->fname = yymsp[-2].minor.yy0;
					set->expr = yymsp[0].minor.yy70;
					yygotominor.yy333 = g_slist_append (NULL, set);
  yy_destructor(yypParser,79,&yymsp[-1].minor);
}
#line 2633 "parser.c"
        break;
      case 71: /* compound ::= selectcmd */
#line 548 "./parser.y"
{
	yygotominor.yy116 = gda_sql_statement_new (GDA_SQL_STATEMENT_COMPOUND);
	gda_sql_statement_compound_take_stmt (yygotominor.yy116, yymsp[0].minor.yy116);
}
#line 2641 "parser.c"
        break;
      case 72: /* compound ::= compound UNION opt_compound_all compound */
#line 552 "./parser.y"
{
	yygotominor.yy116 = compose_multiple_compounds (yymsp[-1].minor.yy276 ? GDA_SQL_STATEMENT_COMPOUND_UNION_ALL : GDA_SQL_STATEMENT_COMPOUND_UNION,
					yymsp[-3].minor.yy116, yymsp[0].minor.yy116);
  yy_destructor(yypParser,114,&yymsp[-2].minor);
}
#line 2650 "parser.c"
        break;
      case 73: /* compound ::= compound EXCEPT opt_compound_all compound */
#line 557 "./parser.y"
{
	yygotominor.yy116 = compose_multiple_compounds (yymsp[-1].minor.yy276 ? GDA_SQL_STATEMENT_COMPOUND_EXCEPT_ALL : GDA_SQL_STATEMENT_COMPOUND_EXCEPT,
					yymsp[-3].minor.yy116, yymsp[0].minor.yy116);
  yy_destructor(yypParser,115,&yymsp[-2].minor);
}
#line 2659 "parser.c"
        break;
      case 74: /* compound ::= compound INTERSECT opt_compound_all compound */
#line 562 "./parser.y"
{
	yygotominor.yy116 = compose_multiple_compounds (yymsp[-1].minor.yy276 ? GDA_SQL_STATEMENT_COMPOUND_INTERSECT_ALL : GDA_SQL_STATEMENT_COMPOUND_INTERSECT,
					yymsp[-3].minor.yy116, yymsp[0].minor.yy116);
  yy_destructor(yypParser,116,&yymsp[-2].minor);
}
#line 2668 "parser.c"
        break;
      case 75: /* opt_compound_all ::= */
#line 568 "./parser.y"
{yygotominor.yy276 = FALSE;}
#line 2673 "parser.c"
        break;
      case 76: /* opt_compound_all ::= ALL */
#line 569 "./parser.y"
{yygotominor.yy276 = TRUE;  yy_destructor(yypParser,134,&yymsp[0].minor);
}
#line 2679 "parser.c"
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
  yy_destructor(yypParser,135,&yymsp[-8].minor);
}
#line 2698 "parser.c"
        break;
      case 78: /* limit_opt ::= */
#line 593 "./parser.y"
{yygotominor.yy44.count = NULL; yygotominor.yy44.offset = NULL;}
#line 2703 "parser.c"
        break;
      case 79: /* limit_opt ::= LIMIT expr */
#line 594 "./parser.y"
{yygotominor.yy44.count = yymsp[0].minor.yy70; yygotominor.yy44.offset = NULL;  yy_destructor(yypParser,136,&yymsp[-1].minor);
}
#line 2709 "parser.c"
        break;
      case 80: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 595 "./parser.y"
{yygotominor.yy44.count = yymsp[-2].minor.yy70; yygotominor.yy44.offset = yymsp[0].minor.yy70;  yy_destructor(yypParser,136,&yymsp[-3].minor);
  yy_destructor(yypParser,33,&yymsp[-1].minor);
}
#line 2716 "parser.c"
        break;
      case 81: /* limit_opt ::= LIMIT expr COMMA expr */
#line 596 "./parser.y"
{yygotominor.yy44.count = yymsp[-2].minor.yy70; yygotominor.yy44.offset = yymsp[0].minor.yy70;  yy_destructor(yypParser,136,&yymsp[-3].minor);
  yy_destructor(yypParser,123,&yymsp[-1].minor);
}
#line 2723 "parser.c"
        break;
      case 82: /* orderby_opt ::= */
#line 600 "./parser.y"
{yygotominor.yy325 = 0;}
#line 2728 "parser.c"
        break;
      case 83: /* orderby_opt ::= ORDER BY sortlist */
#line 601 "./parser.y"
{yygotominor.yy325 = yymsp[0].minor.yy325;  yy_destructor(yypParser,137,&yymsp[-2].minor);
  yy_destructor(yypParser,138,&yymsp[-1].minor);
}
#line 2735 "parser.c"
        break;
      case 84: /* sortlist ::= sortlist COMMA expr sortorder */
#line 605 "./parser.y"
{GdaSqlSelectOrder *order;
							 order = gda_sql_select_order_new (NULL);
							 order->expr = yymsp[-1].minor.yy70;
							 order->asc = yymsp[0].minor.yy276;
							 yygotominor.yy325 = g_slist_append (yymsp[-3].minor.yy325, order);
  yy_destructor(yypParser,123,&yymsp[-2].minor);
}
#line 2746 "parser.c"
        break;
      case 85: /* sortlist ::= expr sortorder */
#line 611 "./parser.y"
{GdaSqlSelectOrder *order;
				       order = gda_sql_select_order_new (NULL);
				       order->expr = yymsp[-1].minor.yy70;
				       order->asc = yymsp[0].minor.yy276;
				       yygotominor.yy325 = g_slist_prepend (NULL, order);
}
#line 2756 "parser.c"
        break;
      case 86: /* sortorder ::= ASC */
#line 619 "./parser.y"
{yygotominor.yy276 = TRUE;  yy_destructor(yypParser,5,&yymsp[0].minor);
}
#line 2762 "parser.c"
        break;
      case 87: /* sortorder ::= DESC */
#line 620 "./parser.y"
{yygotominor.yy276 = FALSE;  yy_destructor(yypParser,14,&yymsp[0].minor);
}
#line 2768 "parser.c"
        break;
      case 88: /* sortorder ::= */
#line 621 "./parser.y"
{yygotominor.yy276 = TRUE;}
#line 2773 "parser.c"
        break;
      case 90: /* having_opt ::= HAVING expr */
#line 627 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;  yy_destructor(yypParser,139,&yymsp[-1].minor);
}
#line 2779 "parser.c"
        break;
      case 91: /* groupby_opt ::= */
#line 631 "./parser.y"
{yygotominor.yy333 = 0;}
#line 2784 "parser.c"
        break;
      case 92: /* groupby_opt ::= GROUP BY rnexprlist */
#line 632 "./parser.y"
{yygotominor.yy333 = g_slist_reverse (yymsp[0].minor.yy325);  yy_destructor(yypParser,140,&yymsp[-2].minor);
  yy_destructor(yypParser,138,&yymsp[-1].minor);
}
#line 2791 "parser.c"
        break;
      case 93: /* from ::= */
      case 98: /* stl_prefix ::= */ yytestcase(yyruleno==98);
#line 636 "./parser.y"
{yygotominor.yy191 = NULL;}
#line 2797 "parser.c"
        break;
      case 94: /* from ::= FROM seltablist */
#line 637 "./parser.y"
{yygotominor.yy191 = yymsp[0].minor.yy191;  yy_destructor(yypParser,130,&yymsp[-1].minor);
}
#line 2803 "parser.c"
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
#line 2822 "parser.c"
        break;
      case 96: /* using_opt ::= USING LP inscollist RP */
#line 662 "./parser.y"
{yygotominor.yy333 = yymsp[-1].minor.yy333;  yy_destructor(yypParser,141,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 2830 "parser.c"
        break;
      case 99: /* stl_prefix ::= seltablist jointype */
#line 666 "./parser.y"
{GdaSqlSelectJoin *join;
					      yygotominor.yy191 = yymsp[-1].minor.yy191;
					      join = gda_sql_select_join_new (GDA_SQL_ANY_PART (yygotominor.yy191));
					      join->type = yymsp[0].minor.yy371;
					      gda_sql_select_from_take_new_join (yygotominor.yy191, join);
}
#line 2840 "parser.c"
        break;
      case 100: /* on_cond ::= ON expr */
#line 676 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;  yy_destructor(yypParser,142,&yymsp[-1].minor);
}
#line 2846 "parser.c"
        break;
      case 102: /* jointype ::= COMMA */
#line 680 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_CROSS;  yy_destructor(yypParser,123,&yymsp[0].minor);
}
#line 2852 "parser.c"
        break;
      case 103: /* jointype ::= JOIN */
#line 681 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_INNER;  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2858 "parser.c"
        break;
      case 104: /* jointype ::= CROSS JOIN */
#line 682 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_CROSS;  yy_destructor(yypParser,113,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2865 "parser.c"
        break;
      case 105: /* jointype ::= INNER JOIN */
#line 683 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_INNER;  yy_destructor(yypParser,108,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2872 "parser.c"
        break;
      case 106: /* jointype ::= NATURAL JOIN */
#line 684 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_NATURAL;  yy_destructor(yypParser,109,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2879 "parser.c"
        break;
      case 107: /* jointype ::= LEFT JOIN */
#line 685 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_LEFT;  yy_destructor(yypParser,110,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2886 "parser.c"
        break;
      case 108: /* jointype ::= LEFT OUTER JOIN */
#line 686 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_LEFT;  yy_destructor(yypParser,110,&yymsp[-2].minor);
  yy_destructor(yypParser,143,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2894 "parser.c"
        break;
      case 109: /* jointype ::= RIGHT JOIN */
#line 687 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_RIGHT;  yy_destructor(yypParser,111,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2901 "parser.c"
        break;
      case 110: /* jointype ::= RIGHT OUTER JOIN */
#line 688 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_RIGHT;  yy_destructor(yypParser,111,&yymsp[-2].minor);
  yy_destructor(yypParser,143,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2909 "parser.c"
        break;
      case 111: /* jointype ::= FULL JOIN */
#line 689 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_FULL;  yy_destructor(yypParser,112,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2916 "parser.c"
        break;
      case 112: /* jointype ::= FULL OUTER JOIN */
#line 690 "./parser.y"
{yygotominor.yy371 = GDA_SQL_SELECT_JOIN_FULL;  yy_destructor(yypParser,112,&yymsp[-2].minor);
  yy_destructor(yypParser,143,&yymsp[-1].minor);
  yy_destructor(yypParser,107,&yymsp[0].minor);
}
#line 2924 "parser.c"
        break;
      case 113: /* seltarget ::= fullname as */
#line 695 "./parser.y"
{yygotominor.yy134 = gda_sql_select_target_new (NULL);
				     gda_sql_select_target_take_alias (yygotominor.yy134, yymsp[0].minor.yy0);
				     gda_sql_select_target_take_table_name (yygotominor.yy134, yymsp[-1].minor.yy0);
}
#line 2932 "parser.c"
        break;
      case 114: /* seltarget ::= fullname ID */
#line 699 "./parser.y"
{yygotominor.yy134 = gda_sql_select_target_new (NULL);
                                     gda_sql_select_target_take_alias (yygotominor.yy134, yymsp[0].minor.yy0);
                                     gda_sql_select_target_take_table_name (yygotominor.yy134, yymsp[-1].minor.yy0);
}
#line 2940 "parser.c"
        break;
      case 115: /* seltarget ::= LP compound RP as */
      case 116: /* seltarget ::= LP compound RP ID */ yytestcase(yyruleno==116);
#line 703 "./parser.y"
{yygotominor.yy134 = gda_sql_select_target_new (NULL);
					     gda_sql_select_target_take_alias (yygotominor.yy134, yymsp[0].minor.yy0);
					     gda_sql_select_target_take_select (yygotominor.yy134, yymsp[-2].minor.yy116);
  yy_destructor(yypParser,105,&yymsp[-3].minor);
  yy_destructor(yypParser,106,&yymsp[-1].minor);
}
#line 2951 "parser.c"
        break;
      case 117: /* sclp ::= selcollist COMMA */
#line 716 "./parser.y"
{yygotominor.yy325 = yymsp[-1].minor.yy325;  yy_destructor(yypParser,123,&yymsp[0].minor);
}
#line 2957 "parser.c"
        break;
      case 118: /* sclp ::= */
      case 133: /* rexprlist ::= */ yytestcase(yyruleno==133);
#line 717 "./parser.y"
{yygotominor.yy325 = NULL;}
#line 2963 "parser.c"
        break;
      case 119: /* selcollist ::= sclp expr as */
#line 719 "./parser.y"
{GdaSqlSelectField *field;
					  field = gda_sql_select_field_new (NULL);
					  gda_sql_select_field_take_expr (field, yymsp[-1].minor.yy70);
					  gda_sql_select_field_take_alias (field, yymsp[0].minor.yy0); 
					  yygotominor.yy325 = g_slist_append (yymsp[-2].minor.yy325, field);}
#line 2972 "parser.c"
        break;
      case 120: /* selcollist ::= sclp starname */
#line 724 "./parser.y"
{GdaSqlSelectField *field;
					field = gda_sql_select_field_new (NULL);
					gda_sql_select_field_take_star_value (field, yymsp[0].minor.yy0);
					yygotominor.yy325 = g_slist_append (yymsp[-1].minor.yy325, field);}
#line 2980 "parser.c"
        break;
      case 122: /* starname ::= nm DOT STAR */
      case 198: /* fullname ::= nm DOT nm */ yytestcase(yyruleno==198);
#line 730 "./parser.y"
{gchar *str;
				  str = g_strdup_printf ("%s.%s", g_value_get_string (yymsp[-2].minor.yy0), g_value_get_string (yymsp[0].minor.yy0));
				  yygotominor.yy0 = g_new0 (GValue, 1);
				  g_value_init (yygotominor.yy0, G_TYPE_STRING);
				  g_value_take_string (yygotominor.yy0, str);
				  g_value_reset (yymsp[-2].minor.yy0); g_free (yymsp[-2].minor.yy0);
				  g_value_reset (yymsp[0].minor.yy0); g_free (yymsp[0].minor.yy0);
  yy_destructor(yypParser,144,&yymsp[-1].minor);
}
#line 2994 "parser.c"
        break;
      case 123: /* starname ::= nm DOT nm DOT STAR */
      case 199: /* fullname ::= nm DOT nm DOT nm */ yytestcase(yyruleno==199);
#line 739 "./parser.y"
{gchar *str;
				  str = g_strdup_printf ("%s.%s.%s", g_value_get_string (yymsp[-4].minor.yy0), 
							 g_value_get_string (yymsp[-2].minor.yy0), g_value_get_string (yymsp[0].minor.yy0));
				  yygotominor.yy0 = g_new0 (GValue, 1);
				  g_value_init (yygotominor.yy0, G_TYPE_STRING);
				  g_value_take_string (yygotominor.yy0, str);
				  g_value_reset (yymsp[-4].minor.yy0); g_free (yymsp[-4].minor.yy0);
				  g_value_reset (yymsp[-2].minor.yy0); g_free (yymsp[-2].minor.yy0);
				  g_value_reset (yymsp[0].minor.yy0); g_free (yymsp[0].minor.yy0);
  yy_destructor(yypParser,144,&yymsp[-3].minor);
  yy_destructor(yypParser,144,&yymsp[-1].minor);
}
#line 3011 "parser.c"
        break;
      case 124: /* as ::= AS fullname */
      case 125: /* as ::= AS value */ yytestcase(yyruleno==125);
#line 750 "./parser.y"
{yygotominor.yy0 = yymsp[0].minor.yy0;  yy_destructor(yypParser,145,&yymsp[-1].minor);
}
#line 3018 "parser.c"
        break;
      case 127: /* distinct ::= */
#line 756 "./parser.y"
{yygotominor.yy297 = NULL;}
#line 3023 "parser.c"
        break;
      case 128: /* distinct ::= ALL */
#line 757 "./parser.y"
{yygotominor.yy297 = NULL;  yy_destructor(yypParser,134,&yymsp[0].minor);
}
#line 3029 "parser.c"
        break;
      case 129: /* distinct ::= DISTINCT */
#line 758 "./parser.y"
{yygotominor.yy297 = g_new0 (Distinct, 1); yygotominor.yy297->distinct = TRUE;  yy_destructor(yypParser,146,&yymsp[0].minor);
}
#line 3035 "parser.c"
        break;
      case 130: /* distinct ::= DISTINCT ON expr */
#line 759 "./parser.y"
{yygotominor.yy297 = g_new0 (Distinct, 1); yygotominor.yy297->distinct = TRUE; yygotominor.yy297->expr = yymsp[0].minor.yy70;  yy_destructor(yypParser,146,&yymsp[-2].minor);
  yy_destructor(yypParser,142,&yymsp[-1].minor);
}
#line 3042 "parser.c"
        break;
      case 131: /* rnexprlist ::= rnexprlist COMMA expr */
      case 134: /* rexprlist ::= rexprlist COMMA expr */ yytestcase(yyruleno==134);
#line 764 "./parser.y"
{yygotominor.yy325 = g_slist_prepend (yymsp[-2].minor.yy325, yymsp[0].minor.yy70);  yy_destructor(yypParser,123,&yymsp[-1].minor);
}
#line 3049 "parser.c"
        break;
      case 132: /* rnexprlist ::= expr */
      case 135: /* rexprlist ::= expr */ yytestcase(yyruleno==135);
#line 765 "./parser.y"
{yygotominor.yy325 = g_slist_append (NULL, yymsp[0].minor.yy70);}
#line 3055 "parser.c"
        break;
      case 136: /* expr ::= pvalue */
#line 777 "./parser.y"
{yygotominor.yy70 = yymsp[0].minor.yy70;}
#line 3060 "parser.c"
        break;
      case 137: /* expr ::= value */
      case 139: /* expr ::= fullname */ yytestcase(yyruleno==139);
#line 778 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->value = yymsp[0].minor.yy0;}
#line 3066 "parser.c"
        break;
      case 138: /* expr ::= LP expr RP */
#line 779 "./parser.y"
{yygotominor.yy70 = yymsp[-1].minor.yy70;  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3073 "parser.c"
        break;
      case 140: /* expr ::= fullname LP rexprlist RP */
#line 781 "./parser.y"
{GdaSqlFunction *func;
					    yygotominor.yy70 = gda_sql_expr_new (NULL); 
					    func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy70)); 
					    gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					    gda_sql_function_take_args_list (func, g_slist_reverse (yymsp[-1].minor.yy325));
					    yygotominor.yy70->func = func;  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3085 "parser.c"
        break;
      case 141: /* expr ::= fullname LP compound RP */
#line 787 "./parser.y"
{GdaSqlFunction *func;
					     GdaSqlExpr *expr;
					     yygotominor.yy70 = gda_sql_expr_new (NULL); 
					     func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy70)); 
					     gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					     expr = gda_sql_expr_new (GDA_SQL_ANY_PART (func)); 
					     gda_sql_expr_take_select (expr, yymsp[-1].minor.yy116);
					     gda_sql_function_take_args_list (func, g_slist_prepend (NULL, expr));
					     yygotominor.yy70->func = func;  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3100 "parser.c"
        break;
      case 142: /* expr ::= fullname LP starname RP */
#line 796 "./parser.y"
{GdaSqlFunction *func;
					    GdaSqlExpr *expr;
					    yygotominor.yy70 = gda_sql_expr_new (NULL); 
					    func = gda_sql_function_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					    gda_sql_function_take_name (func, yymsp[-3].minor.yy0);
					    expr = gda_sql_expr_new (GDA_SQL_ANY_PART (func)); 
					    expr->value = yymsp[-1].minor.yy0;
					    gda_sql_function_take_args_list (func, g_slist_prepend (NULL, expr));
					    yygotominor.yy70->func = func;  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3115 "parser.c"
        break;
      case 143: /* expr ::= CAST LP expr AS fullname RP */
#line 805 "./parser.y"
{yygotominor.yy70 = yymsp[-3].minor.yy70;
						yymsp[-3].minor.yy70->cast_as = g_value_dup_string (yymsp[-1].minor.yy0);
						g_value_reset (yymsp[-1].minor.yy0);
						g_free (yymsp[-1].minor.yy0);  yy_destructor(yypParser,10,&yymsp[-5].minor);
  yy_destructor(yypParser,105,&yymsp[-4].minor);
  yy_destructor(yypParser,145,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3127 "parser.c"
        break;
      case 144: /* expr ::= expr PGCAST fullname */
#line 809 "./parser.y"
{yygotominor.yy70 = yymsp[-2].minor.yy70;
					 yymsp[-2].minor.yy70->cast_as = g_value_dup_string (yymsp[0].minor.yy0);
					 g_value_reset (yymsp[0].minor.yy0);
					 g_free (yymsp[0].minor.yy0);  yy_destructor(yypParser,117,&yymsp[-1].minor);
}
#line 3136 "parser.c"
        break;
      case 145: /* expr ::= expr PLUS|MINUS expr */
#line 814 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (string_to_op_type (yymsp[-1].minor.yy0), yymsp[-2].minor.yy70, yymsp[0].minor.yy70);}
#line 3141 "parser.c"
        break;
      case 146: /* expr ::= expr STAR expr */
#line 815 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_STAR, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,97,&yymsp[-1].minor);
}
#line 3147 "parser.c"
        break;
      case 147: /* expr ::= expr SLASH|REM expr */
      case 148: /* expr ::= expr BITAND|BITOR expr */ yytestcase(yyruleno==148);
      case 154: /* expr ::= expr GT|LEQ|GEQ|LT expr */ yytestcase(yyruleno==154);
      case 155: /* expr ::= expr DIFF|EQ expr */ yytestcase(yyruleno==155);
      case 160: /* expr ::= expr REGEXP|REGEXP_CI|NOT_REGEXP|NOT_REGEXP_CI|SIMILAR expr */ yytestcase(yyruleno==160);
#line 816 "./parser.y"
{yygotominor.yy70 = create_two_expr (string_to_op_type (yymsp[-1].minor.yy0), yymsp[-2].minor.yy70, yymsp[0].minor.yy70);}
#line 3156 "parser.c"
        break;
      case 149: /* expr ::= MINUS expr */
#line 819 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_MINUS, yymsp[0].minor.yy70);  yy_destructor(yypParser,96,&yymsp[-1].minor);
}
#line 3162 "parser.c"
        break;
      case 150: /* expr ::= PLUS expr */
#line 820 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_PLUS, yymsp[0].minor.yy70);  yy_destructor(yypParser,95,&yymsp[-1].minor);
}
#line 3168 "parser.c"
        break;
      case 151: /* expr ::= expr AND expr */
#line 822 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_AND, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,70,&yymsp[-1].minor);
}
#line 3174 "parser.c"
        break;
      case 152: /* expr ::= expr OR expr */
#line 823 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_OR, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,69,&yymsp[-1].minor);
}
#line 3180 "parser.c"
        break;
      case 153: /* expr ::= expr CONCAT expr */
#line 824 "./parser.y"
{yygotominor.yy70 = compose_multiple_expr (GDA_SQL_OPERATOR_TYPE_CONCAT, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,100,&yymsp[-1].minor);
}
#line 3186 "parser.c"
        break;
      case 156: /* expr ::= expr LIKE expr */
#line 828 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_LIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,26,&yymsp[-1].minor);
}
#line 3192 "parser.c"
        break;
      case 157: /* expr ::= expr ILIKE expr */
#line 829 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_ILIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,27,&yymsp[-1].minor);
}
#line 3198 "parser.c"
        break;
      case 158: /* expr ::= expr NOTLIKE expr */
#line 830 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_NOTLIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,73,&yymsp[-1].minor);
}
#line 3204 "parser.c"
        break;
      case 159: /* expr ::= expr NOTILIKE expr */
#line 831 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_NOTILIKE, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,74,&yymsp[-1].minor);
}
#line 3210 "parser.c"
        break;
      case 161: /* expr ::= expr BETWEEN expr AND expr */
#line 833 "./parser.y"
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
  yy_destructor(yypParser,80,&yymsp[-3].minor);
  yy_destructor(yypParser,70,&yymsp[-1].minor);
}
#line 3228 "parser.c"
        break;
      case 162: /* expr ::= expr NOT BETWEEN expr AND expr */
#line 846 "./parser.y"
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
  yy_destructor(yypParser,71,&yymsp[-4].minor);
  yy_destructor(yypParser,80,&yymsp[-3].minor);
  yy_destructor(yypParser,70,&yymsp[-1].minor);
}
#line 3255 "parser.c"
        break;
      case 163: /* expr ::= NOT expr */
#line 867 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_NOT, yymsp[0].minor.yy70);  yy_destructor(yypParser,71,&yymsp[-1].minor);
}
#line 3261 "parser.c"
        break;
      case 164: /* expr ::= BITNOT expr */
#line 868 "./parser.y"
{yygotominor.yy70 = create_uni_expr (GDA_SQL_OPERATOR_TYPE_BITNOT, yymsp[0].minor.yy70);  yy_destructor(yypParser,104,&yymsp[-1].minor);
}
#line 3267 "parser.c"
        break;
      case 165: /* expr ::= expr uni_op */
#line 869 "./parser.y"
{yygotominor.yy70 = create_uni_expr (yymsp[0].minor.yy147, yymsp[-1].minor.yy70);}
#line 3272 "parser.c"
        break;
      case 166: /* expr ::= expr IS expr */
#line 871 "./parser.y"
{yygotominor.yy70 = create_two_expr (GDA_SQL_OPERATOR_TYPE_IS, yymsp[-2].minor.yy70, yymsp[0].minor.yy70);  yy_destructor(yypParser,72,&yymsp[-1].minor);
}
#line 3278 "parser.c"
        break;
      case 167: /* expr ::= LP compound RP */
#line 872 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); gda_sql_expr_take_select (yygotominor.yy70, yymsp[-1].minor.yy116);  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3285 "parser.c"
        break;
      case 168: /* expr ::= expr IN LP rexprlist RP */
#line 873 "./parser.y"
{GdaSqlOperation *cond;
					   GSList *list;
					   yygotominor.yy70 = gda_sql_expr_new (NULL);
					   cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					   yygotominor.yy70->cond = cond;
					   cond->operator_type = GDA_SQL_OPERATOR_TYPE_IN;
					   cond->operands = g_slist_prepend (g_slist_reverse (yymsp[-1].minor.yy325), yymsp[-4].minor.yy70);
					   for (list = cond->operands; list; list = list->next)
						   GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,75,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3302 "parser.c"
        break;
      case 169: /* expr ::= expr IN LP compound RP */
#line 883 "./parser.y"
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
  yy_destructor(yypParser,75,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3322 "parser.c"
        break;
      case 170: /* expr ::= expr NOT IN LP rexprlist RP */
#line 896 "./parser.y"
{GdaSqlOperation *cond;
					       GSList *list;
					       yygotominor.yy70 = gda_sql_expr_new (NULL);
					       cond = gda_sql_operation_new (GDA_SQL_ANY_PART (yygotominor.yy70));
					       yygotominor.yy70->cond = cond;
					       cond->operator_type = GDA_SQL_OPERATOR_TYPE_NOTIN;
					       cond->operands = g_slist_prepend (g_slist_reverse (yymsp[-1].minor.yy325), yymsp[-5].minor.yy70);
					       for (list = cond->operands; list; list = list->next)
						       GDA_SQL_ANY_PART (list->data)->parent = GDA_SQL_ANY_PART (cond);
  yy_destructor(yypParser,71,&yymsp[-4].minor);
  yy_destructor(yypParser,75,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3340 "parser.c"
        break;
      case 171: /* expr ::= expr NOT IN LP compound RP */
#line 906 "./parser.y"
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
  yy_destructor(yypParser,71,&yymsp[-4].minor);
  yy_destructor(yypParser,75,&yymsp[-3].minor);
  yy_destructor(yypParser,105,&yymsp[-2].minor);
  yy_destructor(yypParser,106,&yymsp[0].minor);
}
#line 3361 "parser.c"
        break;
      case 172: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 919 "./parser.y"
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
  yy_destructor(yypParser,147,&yymsp[-4].minor);
  yy_destructor(yypParser,17,&yymsp[0].minor);
}
#line 3382 "parser.c"
        break;
      case 173: /* case_operand ::= expr */
#line 937 "./parser.y"
{yygotominor.yy146 = yymsp[0].minor.yy70;}
#line 3387 "parser.c"
        break;
      case 174: /* case_operand ::= */
      case 178: /* case_else ::= */ yytestcase(yyruleno==178);
#line 938 "./parser.y"
{yygotominor.yy146 = NULL;}
#line 3393 "parser.c"
        break;
      case 175: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 944 "./parser.y"
{
	yygotominor.yy59.when_list = g_slist_append (yymsp[-4].minor.yy59.when_list, yymsp[-2].minor.yy70);
	yygotominor.yy59.then_list = g_slist_append (yymsp[-4].minor.yy59.then_list, yymsp[0].minor.yy70);
  yy_destructor(yypParser,148,&yymsp[-3].minor);
  yy_destructor(yypParser,149,&yymsp[-1].minor);
}
#line 3403 "parser.c"
        break;
      case 176: /* case_exprlist ::= WHEN expr THEN expr */
#line 948 "./parser.y"
{
	yygotominor.yy59.when_list = g_slist_prepend (NULL, yymsp[-2].minor.yy70);
	yygotominor.yy59.then_list = g_slist_prepend (NULL, yymsp[0].minor.yy70);
  yy_destructor(yypParser,148,&yymsp[-3].minor);
  yy_destructor(yypParser,149,&yymsp[-1].minor);
}
#line 3413 "parser.c"
        break;
      case 177: /* case_else ::= ELSE expr */
#line 955 "./parser.y"
{yygotominor.yy146 = yymsp[0].minor.yy70;  yy_destructor(yypParser,150,&yymsp[-1].minor);
}
#line 3419 "parser.c"
        break;
      case 179: /* uni_op ::= ISNULL */
#line 959 "./parser.y"
{yygotominor.yy147 = GDA_SQL_OPERATOR_TYPE_ISNULL;  yy_destructor(yypParser,76,&yymsp[0].minor);
}
#line 3425 "parser.c"
        break;
      case 180: /* uni_op ::= IS NOTNULL */
#line 960 "./parser.y"
{yygotominor.yy147 = GDA_SQL_OPERATOR_TYPE_ISNOTNULL;  yy_destructor(yypParser,72,&yymsp[-1].minor);
  yy_destructor(yypParser,77,&yymsp[0].minor);
}
#line 3432 "parser.c"
        break;
      case 181: /* value ::= NULL */
#line 964 "./parser.y"
{yygotominor.yy0 = NULL;  yy_destructor(yypParser,151,&yymsp[0].minor);
}
#line 3438 "parser.c"
        break;
      case 185: /* pvalue ::= UNSPECVAL LSBRACKET paramspec RSBRACKET */
#line 973 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->param_spec = yymsp[-1].minor.yy339;  yy_destructor(yypParser,153,&yymsp[-3].minor);
  yy_destructor(yypParser,154,&yymsp[-2].minor);
  yy_destructor(yypParser,155,&yymsp[0].minor);
}
#line 3446 "parser.c"
        break;
      case 186: /* pvalue ::= value LSBRACKET paramspec RSBRACKET */
#line 974 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->value = yymsp[-3].minor.yy0; yygotominor.yy70->param_spec = yymsp[-1].minor.yy339;  yy_destructor(yypParser,154,&yymsp[-2].minor);
  yy_destructor(yypParser,155,&yymsp[0].minor);
}
#line 3453 "parser.c"
        break;
      case 187: /* pvalue ::= SIMPLEPARAM */
#line 975 "./parser.y"
{yygotominor.yy70 = gda_sql_expr_new (NULL); yygotominor.yy70->param_spec = gda_sql_param_spec_new (yymsp[0].minor.yy0);}
#line 3458 "parser.c"
        break;
      case 188: /* paramspec ::= */
#line 980 "./parser.y"
{yygotominor.yy339 = NULL;}
#line 3463 "parser.c"
        break;
      case 189: /* paramspec ::= paramspec PNAME */
#line 981 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					 gda_sql_param_spec_take_name (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3469 "parser.c"
        break;
      case 190: /* paramspec ::= paramspec PDESCR */
#line 983 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					 gda_sql_param_spec_take_descr (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3475 "parser.c"
        break;
      case 191: /* paramspec ::= paramspec PTYPE */
#line 985 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					 gda_sql_param_spec_take_type (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3481 "parser.c"
        break;
      case 192: /* paramspec ::= paramspec PNULLOK */
#line 987 "./parser.y"
{if (!yymsp[-1].minor.yy339) yygotominor.yy339 = gda_sql_param_spec_new (NULL); else yygotominor.yy339 = yymsp[-1].minor.yy339; 
					   gda_sql_param_spec_take_nullok (yygotominor.yy339, yymsp[0].minor.yy0);}
#line 3487 "parser.c"
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
  priv_gda_sql_parserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
  priv_gda_sql_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
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
  priv_gda_sql_parserARG_FETCH;
#define TOKEN (yyminor.yy0)
#line 22 "./parser.y"

	gda_sql_parser_set_syntax_error (pdata->parser);
#line 3554 "parser.c"
  priv_gda_sql_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  priv_gda_sql_parserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yyidx>=0 ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
  priv_gda_sql_parserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "priv_gda_sql_parserAlloc" which describes the current state of the parser.
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
void priv_gda_sql_parser(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  priv_gda_sql_parserTOKENTYPE yyminor       /* The value for the token */
  priv_gda_sql_parserARG_PDECL               /* Optional %extra_argument parameter */
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
  priv_gda_sql_parserARG_STORE;

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
