#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>

#include "picoc.h"


#define isCidstart(c) (isalpha(c) || (c)=='_' || (c)=='#')
#define isCident(c) (isalnum(c) || (c)=='_')

#define ISVALUETOKEN(t) ((t) >= TokenIdentifier && (t) <= TokenCharacterConstant)

#define NEXTIS(c,x,y) { if (NextChar == (c)) { Lexer->Pos++; GotToken = (x); } else GotToken = (y); }
#define NEXTIS3(c,x,d,y,z) { if (NextChar == (c)) { Lexer->Pos++; GotToken = (x); } else NEXTIS(d,y,z) }
#define NEXTIS4(c,x,d,y,e,z,a) { if (NextChar == (c)) { Lexer->Pos++; GotToken = (x); } else NEXTIS3(d,y,e,z,a) }

static union AnyValue LexAnyValue;
static struct Value LexValue = { TypeVoid, &LexAnyValue, FALSE, FALSE };

struct ReservedWord
{
    const char *Word;
    enum LexToken Token;
    const char *SharedWord; /* word stored in shared string space */
};

static struct ReservedWord ReservedWords[] =
{
    { "#define", TokenHashDefine, NULL },
    { "#include", TokenHashInclude, NULL },
    { "break", TokenBreak, NULL },
    { "case", TokenCase, NULL },
    { "char", TokenCharType, NULL },
    { "default", TokenDefault, NULL },
    { "do", TokenDo, NULL },
    { "double", TokenDoubleType, NULL },
    { "else", TokenElse, NULL },
    { "enum", TokenEnumType, NULL },
    { "float", TokenFloatType, NULL },
    { "for", TokenFor, NULL },
    { "if", TokenIf, NULL },
    { "int", TokenIntType, NULL },
    { "long", TokenLongType, NULL },
    { "return", TokenReturn, NULL },
    { "signed", TokenSignedType, NULL },
    { "short", TokenShortType, NULL },
    { "struct", TokenStructType, NULL },
    { "switch", TokenSwitch, NULL },
    { "typedef", TokenTypedef, NULL },
    { "union", TokenUnionType, NULL },
    { "unsigned", TokenUnsignedType, NULL },
    { "void", TokenVoidType, NULL },
    { "while", TokenWhile, NULL }
};

struct LexState
{
    const char *Pos;
    const char *End;
    int Line;
    const char *FileName;
};


/* initialise the lexer */
void LexInit()
{
    int Count;
    
    for (Count = 0; Count < sizeof(ReservedWords) / sizeof(struct ReservedWord); Count++)
        ReservedWords[Count].SharedWord = StrRegister(ReservedWords[Count].Word);
}

/* exit with a message */
void LexFail(struct LexState *Lexer, const char *Message, ...)
{
    va_list Args;

    printf("%s:%d: ", Lexer->FileName, Lexer->Line);      
    va_start(Args, Message);
    vprintf(Message, Args);
    printf("\n");
    exit(1);
}

/* check if a word is a reserved word - used while scanning */
enum LexToken LexCheckReservedWord(const char *Word)
{
    int Count;
    
    for (Count = 0; Count < sizeof(ReservedWords) / sizeof(struct ReservedWord); Count++)
    {
        if (Word == ReservedWords[Count].SharedWord)
            return ReservedWords[Count].Token;
    }
    
    return TokenNone;
}

/* skip a comment - used while scanning */
enum LexToken LexGetNumber(struct LexState *Lexer, struct Value *Value)
{
    int Result = 0;
    double FPResult;
    double FPDiv;
    
    for (; Lexer->Pos != Lexer->End && isdigit(*Lexer->Pos); Lexer->Pos++)
        Result = Result * 10 + (*Lexer->Pos - '0');

    Value->Typ = &IntType;
    Value->Val->Integer = Result;
    if (Lexer->Pos == Lexer->End || *Lexer->Pos != '.')
        return TokenIntegerConstant;

    Value->Typ = &FPType;
    Lexer->Pos++;
    for (FPDiv = 0.1, FPResult = (double)Result; Lexer->Pos != Lexer->End && isdigit(*Lexer->Pos); Lexer->Pos++, FPDiv /= 10.0)
        FPResult += (*Lexer->Pos - '0') * FPDiv;
    
    if (Lexer->Pos != Lexer->End && (*Lexer->Pos == 'e' || *Lexer->Pos == 'E'))
    {
        Lexer->Pos++;
        for (Result = 0; Lexer->Pos != Lexer->End && isdigit(*Lexer->Pos); Lexer->Pos++)
            Result = Result * 10 + (*Lexer->Pos - '0');
            
        FPResult *= pow(10.0, (double)Result);
    }
    
    return TokenFPConstant;
}

/* get a reserved word or identifier - used while scanning */
enum LexToken LexGetWord(struct LexState *Lexer, struct Value *Value)
{
    const char *Pos = Lexer->Pos + 1;
    enum LexToken Token;
    
    while (Lexer->Pos != Lexer->End && isCident(*Pos))
        Pos++;
    
    Value->Typ = &StringType;
    Value->Val->String = (char *)StrRegister2(Lexer->Pos, Pos - Lexer->Pos);
    Lexer->Pos = Pos;
    
    Token = LexCheckReservedWord(Value->Val->String);
    if (Token != TokenNone)
        return Token;
    
    return TokenIdentifier;
}

/* get a string constant - used while scanning */
enum LexToken LexGetStringConstant(struct LexState *Lexer, struct Value *Value)
{
    int Escape = FALSE;
    const char *StartPos = Lexer->Pos;
    
    // XXX - do escaping here
    Value->Typ = &StringType;
    while (Lexer->Pos != Lexer->End && (*Lexer->Pos != '"' || Escape))
    {
        if (Escape)
            Escape = FALSE;
        else if (*Lexer->Pos == '\\')
            Escape = TRUE;
            
        Lexer->Pos++;
    }
    Value->Val->String = (char *)StrRegister2(StartPos, Lexer->Pos - StartPos);
    if (*Lexer->Pos == '"')
        Lexer->Pos++;
    
    return TokenStringConstant;
}

/* get a character constant - used while scanning */
enum LexToken LexGetCharacterConstant(struct LexState *Lexer, struct Value *Value)
{
    Value->Typ = &IntType;
    Value->Val->Integer = Lexer->Pos[1];
    if (Lexer->Pos[2] != '\'')
        LexFail(Lexer, "illegal character '%c'", Lexer->Pos[2]);
        
    Lexer->Pos += 3;
    return TokenCharacterConstant;
}

/* skip a comment - used while scanning */
void LexSkipComment(struct LexState *Lexer, char NextChar)
{
    Lexer->Pos++;
    if (NextChar == '*')
    {   /* conventional C comment */
        while (Lexer->Pos != Lexer->End && (*(Lexer->Pos-1) != '*' || *Lexer->Pos != '/'))
            Lexer->Pos++;
        
        if (Lexer->Pos != Lexer->End)
            Lexer->Pos++;
    }
    else
    {   /* C++ style comment */
        while (Lexer->Pos != Lexer->End && *Lexer->Pos != '\n')
            Lexer->Pos++;
    }
}

/* get a single token from the source - used while scanning */
enum LexToken LexScanGetToken(struct LexState *Lexer, struct Value **Value)
{
    char ThisChar;
    char NextChar;
    enum LexToken GotToken = TokenNone;
    
    do
    {
        if (Lexer->Pos == Lexer->End)
        {
            char LineBuffer[LINEBUFFER_MAX];
            if (fgets(&LineBuffer[0], LINEBUFFER_MAX, stdin) == NULL)
                return TokenEOF;
        }
        
        *Value = &LexValue;
        while (Lexer->Pos != Lexer->End && isspace(*Lexer->Pos))
        {
            if (*Lexer->Pos == '\n')
                Lexer->Line++;
    
            Lexer->Pos++;
        }
        
        ThisChar = *Lexer->Pos;
        if (isCidstart(ThisChar))
            return LexGetWord(Lexer, *Value);
        
        if (isdigit(ThisChar))
            return LexGetNumber(Lexer, *Value);
        
        NextChar = (Lexer->Pos+1 != Lexer->End) ? *(Lexer->Pos+1) : 0;
        Lexer->Pos++;
        switch (ThisChar)
        {
            case '"': GotToken = LexGetStringConstant(Lexer, *Value);
            case '\'': GotToken = LexGetCharacterConstant(Lexer, *Value);
            case '(': GotToken = TokenOpenBracket;
            case ')': GotToken = TokenCloseBracket;
            case '=': NEXTIS('=', TokenEquality, TokenAssign);
            case '+': NEXTIS3('=', TokenAddAssign, '+', TokenIncrement, TokenPlus);
            case '-': NEXTIS4('=', TokenSubtractAssign, '>', TokenArrow, '-', TokenDecrement, TokenMinus);
            case '*': GotToken = TokenAsterisk;
            case '/': if (NextChar == '/' || NextChar == '*') LexSkipComment(Lexer, NextChar); else GotToken = TokenSlash;
            case '<': NEXTIS('=', TokenLessEqual, TokenLessThan);
            case '>': NEXTIS('=', TokenGreaterEqual, TokenGreaterThan);
            case ';': GotToken = TokenSemicolon;
            case '&': NEXTIS('&', TokenLogicalAnd, TokenAmpersand);
            case '|': NEXTIS('|', TokenLogicalOr, TokenArithmeticOr);
            case '{': GotToken = TokenLeftBrace;
            case '}': GotToken = TokenRightBrace;
            case '[': GotToken = TokenLeftSquareBracket;
            case ']': GotToken = TokenRightSquareBracket;
            case '!': GotToken = TokenUnaryNot;
            case '^': GotToken = TokenArithmeticExor;
            case '~': GotToken = TokenUnaryExor;
            case ',': GotToken = TokenComma;
            case '.': GotToken = TokenDot;
            default:  LexFail(Lexer, "illegal character '%c'", ThisChar);
        }
    } while (GotToken == TokenNone);
    
    return GotToken;
}

/* produce tokens from the lexer and return a heap buffer with the result - used for scanning */
void *LexTokenise(struct LexState *Lexer)
{
    enum LexToken Token;
    void *HeapMem;
    struct Value *GotValue;
    int MemAvailable;
    int MemUsed = 0;
    void *TokenSpace = HeapStackGetFreeSpace(&MemAvailable);
    
    do
    { /* store the token at the end of the stack area */
        Token = LexScanGetToken(Lexer, &GotValue);
        *(char *)TokenSpace = Token;
        TokenSpace++;
        MemUsed++;

        if (ISVALUETOKEN(Token))
        { /* store a value as well */
            int ValueSize = sizeof(struct Value) + GotValue->Typ->Sizeof;
            if (MemAvailable - MemUsed <= ValueSize)
                LexFail(Lexer, "out of memory while lexing");

            memcpy(TokenSpace, GotValue, ValueSize);
            TokenSpace += ValueSize;
            MemUsed += ValueSize;
        }
        
        if (MemAvailable <= MemUsed)
            LexFail(Lexer, "out of memory while lexing");
            
    } while (Token != TokenEOF);
    
    if (MemAvailable < MemUsed*2 + sizeof(struct AllocNode))   /* need memory for stack copy + heap copy */
        LexFail(Lexer, "out of memory while lexing");
        
    HeapMem = HeapAlloc(MemUsed);
    memcpy(HeapMem, HeapStackGetFreeSpace(&MemAvailable), MemUsed);    
    return HeapMem;
}

/* lexically analyse some source text */
void *LexAnalyse(const char *FileName, const char *Source, int SourceLen)
{
    struct LexState Lexer;
    
    Lexer.Pos = Source;
    Lexer.End = Source + SourceLen;
    Lexer.Line = 1;
    Lexer.FileName = FileName;
    return LexTokenise(&Lexer);
}

/* prepare to parse a pre-tokenised buffer */
void LexInitParser(struct ParseState *Parser, void *TokenSource, const char *FileName, int Line)
{
    Parser->Pos = TokenSource;
    Parser->Line = Line;
    Parser->FileName = FileName;
}

/* get the next token given a parser state */
enum LexToken LexGetToken(struct ParseState *Parser, struct Value **Value, int IncPos)
{
    enum LexToken Token;
        
    while ((enum LexToken)*(unsigned char *)Parser->Pos == TokenEndOfLine)
    { /* skip leading newlines */
        Parser->Line++;
        Parser->Pos++;
    }
    
    Token = (enum LexToken)*(unsigned char *)Parser->Pos;
    if (ISVALUETOKEN(Token))
    { /* this token requires a value */
        int ValueLen = sizeof(struct Value) + ((struct Value *)Parser->Pos)->Typ->Sizeof;
        if (Value != NULL)
        { /* copy the value out (aligns it in the process) */
            memcpy(&LexValue, (struct Value *)Parser->Pos, ValueLen);
            *Value = &LexValue;
        }
        
        if (IncPos)
            Parser->Pos += ValueLen + 1;
    }
    else
    {
        if (IncPos && Token != TokenEndOfLine)
            Parser->Pos++;
    }
    
    return Token;
}

