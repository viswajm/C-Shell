#include "../include/parser.h"

// ---------------- Lexer ----------------
typedef enum
{
    T_NAME,
    T_PIPE,
    T_AMP,
    T_SEMI,
    T_LT,
    T_GT,
    T_GTGT,
    T_LTNAME,
    T_GTNAME,
    T_GTGTNAME,
    T_END,
    T_INVALID
} TokenType;

typedef struct
{
    TokenType type;
    char text[256];
} Token;

typedef struct
{
    const char *input;
    size_t pos;
    Token current;
} Lexer;

static void next_token(Lexer *lex);

static bool is_special(char c)
{
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

static void init_lexer(Lexer *lex, const char *input)
{
    lex->input = input;
    lex->pos = 0;
    next_token(lex);
}

static void skip_ws(Lexer *lex)
{
    while (isspace((unsigned char)lex->input[lex->pos]))
        lex->pos++;
}

static void read_name(Lexer *lex, char *buf, size_t max)
{
    int i = 0;
    while (lex->input[lex->pos] &&
           !isspace((unsigned char)lex->input[lex->pos]) &&
           !is_special(lex->input[lex->pos]))
    {
        if (i < (int)max - 1)
            buf[i++] = lex->input[lex->pos];
        lex->pos++;
    }
    buf[i] = '\0';
}

static void next_token(Lexer *lex)
{
    skip_ws(lex);
    char c = lex->input[lex->pos];
    if (c == '\0')
    {
        lex->current.type = T_END;
        return;
    }

    if (c == '|')
    {
        lex->current.type = T_PIPE;
        strcpy(lex->current.text, "|");
        lex->pos++;
    }
    else if (c == '&')
    {
        lex->current.type = T_AMP;
        strcpy(lex->current.text, "&");
        lex->pos++;
    }
    else if (c == ';')
    {
        lex->current.type = T_SEMI;
        strcpy(lex->current.text, ";");
        lex->pos++;
    }
    else if (c == '<')
    {
        lex->pos++;
        if (lex->input[lex->pos] &&
            !isspace((unsigned char)lex->input[lex->pos]) &&
            !is_special(lex->input[lex->pos]))
        {
            lex->current.type = T_LTNAME;
            read_name(lex, lex->current.text, sizeof(lex->current.text));
        }
        else
        {
            lex->current.type = T_LT;
            strcpy(lex->current.text, "<");
        }
    }
    else if (c == '>')
    {
        if (lex->input[lex->pos + 1] == '>')
        {
            lex->pos += 2;
            if (lex->input[lex->pos] &&
                !isspace((unsigned char)lex->input[lex->pos]) &&
                !is_special(lex->input[lex->pos]))
            {
                lex->current.type = T_GTGTNAME;
                read_name(lex, lex->current.text, sizeof(lex->current.text));
            }
            else
            {
                lex->current.type = T_GTGT;
                strcpy(lex->current.text, ">>");
            }
        }
        else
        {
            lex->pos++;
            if (lex->input[lex->pos] &&
                !isspace((unsigned char)lex->input[lex->pos]) &&
                !is_special(lex->input[lex->pos]))
            {
                lex->current.type = T_GTNAME;
                read_name(lex, lex->current.text, sizeof(lex->current.text));
            }
            else
            {
                lex->current.type = T_GT;
                strcpy(lex->current.text, ">");
            }
        }
    }
    else
    {
        // parse NAME
        read_name(lex, lex->current.text, sizeof(lex->current.text));
        lex->current.type = (lex->current.text[0] != '\0') ? T_NAME : T_INVALID;
    }
}

// ---------------- Parser ----------------
typedef struct
{
    Lexer lex;
    bool error;
} Parser;

static void consume(Parser *p, TokenType t)
{
    if (p->lex.current.type == t)
    {
        next_token(&p->lex);
    }
    else
    {
        p->error = true;
    }
}

// Forward declarations
static void parse_shell_cmd(Parser *p);
static void parse_cmd_group(Parser *p);
static void parse_atomic(Parser *p);
static void parse_input(Parser *p);
static void parse_output(Parser *p);

// shell_cmd -> cmd_group ((& | ;) cmd_group)* &?
// shell_cmd -> cmd_group ((; cmd_group) | (& cmd_group))* &?
static void parse_shell_cmd(Parser *p)
{
    parse_cmd_group(p);

    // loop for ; or & followed by cmd_group
    while (!p->error &&
           (p->lex.current.type == T_SEMI ||
           (p->lex.current.type == T_AMP && p->lex.input[p->lex.pos] != '\0')))
    {
        TokenType op = p->lex.current.type;
        consume(p, op);
        parse_cmd_group(p);
    }

    // final optional &
    if (!p->error && p->lex.current.type == T_AMP)
    {
        consume(p, T_AMP);
    }
}


// cmd_group -> atomic (| atomic)*
static void parse_cmd_group(Parser *p)
{
    parse_atomic(p);
    while (!p->error && p->lex.current.type == T_PIPE)
    {
        consume(p, T_PIPE);
        parse_atomic(p);
    }
}

// atomic -> name (name | input | output)*
static void parse_atomic(Parser *p)
{
    if (p->lex.current.type != T_NAME)
    {
        p->error = true;
        return;
    }
    consume(p, T_NAME);
    while (!p->error)
    {
        if (p->lex.current.type == T_NAME)
        {
            consume(p, T_NAME);
        }
        else if (p->lex.current.type == T_LT || p->lex.current.type == T_LTNAME)
        {
            parse_input(p);
        }
        else if (p->lex.current.type == T_GT || p->lex.current.type == T_GTGT ||
                 p->lex.current.type == T_GTNAME || p->lex.current.type == T_GTGTNAME)
        {
            parse_output(p);
        }
        else
        {
            break;
        }
    }
}

// input -> < name | <name
static void parse_input(Parser *p)
{
    if (p->lex.current.type == T_LT)
    {
        consume(p, T_LT);
        if (p->lex.current.type == T_NAME)
            consume(p, T_NAME);
        else
            p->error = true;
    }
    else if (p->lex.current.type == T_LTNAME)
    {
        consume(p, T_LTNAME);
    }
    else
    {
        p->error = true;
    }
}

// output -> > name | >name | >> name | >>name
static void parse_output(Parser *p)
{
    if (p->lex.current.type == T_GT)
    {
        consume(p, T_GT);
        if (p->lex.current.type == T_NAME)
            consume(p, T_NAME);
        else
            p->error = true;
    }
    else if (p->lex.current.type == T_GTGT)
    {
        consume(p, T_GTGT);
        if (p->lex.current.type == T_NAME)
            consume(p, T_NAME);
        else
            p->error = true;
    }
    else if (p->lex.current.type == T_GTNAME || p->lex.current.type == T_GTGTNAME)
    {
        consume(p, p->lex.current.type);
    }
    else
    {
        p->error = true;
    }
}

// ---------------- Public API ----------------
bool validate_command(const char *input)
{
    Parser p;
    init_lexer(&p.lex, input);
    p.error = false;

    parse_shell_cmd(&p);

    if (!p.error && p.lex.current.type == T_END)
    {
        return true;
    }
    return false;
}
