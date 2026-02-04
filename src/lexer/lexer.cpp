#include <cctype>
#include <cstddef>
#include <curlee/lexer/lexer.h>
#include <optional>

namespace curlee::lexer
{
namespace
{

class Lexer
{
  public:
    explicit Lexer(std::string_view input) : input_(input) {}

    [[nodiscard]] LexResult lex_all()
    {
        std::vector<Token> tokens;
        while (true)
        {
            if (auto err = skip_trivia())
            {
                return *err;
            }

            const std::size_t start = pos_;
            if (is_at_end())
            {
                tokens.push_back(Token{
                    .kind = TokenKind::Eof, .lexeme = std::string_view{}, .span = {start, start}});
                return tokens;
            }

            const char c = peek();

            const auto next = peek_next();

            // Identifiers / keywords
            if (is_ident_start(c))
            {
                advance();
                while (!is_at_end() && is_ident_continue(peek()))
                {
                    advance();
                }
                const std::string_view lexeme = input_.substr(start, pos_ - start);
                tokens.push_back(Token{
                    .kind = keyword_or_ident(lexeme), .lexeme = lexeme, .span = {start, pos_}});
                continue;
            }

            // Numbers (integer literals)
            if (std::isdigit(static_cast<unsigned char>(c)) != 0)
            {
                advance();
                while (!is_at_end() && (std::isdigit(static_cast<unsigned char>(peek())) != 0))
                {
                    advance();
                }
                const std::string_view lexeme = input_.substr(start, pos_ - start);
                tokens.push_back(
                    Token{.kind = TokenKind::IntLiteral, .lexeme = lexeme, .span = {start, pos_}});
                continue;
            }

            // Strings (MVP: double-quoted, basic escapes; no interpolation)
            if (c == '"')
            {
                advance();
                while (!is_at_end())
                {
                    const char ch = peek();
                    if (ch == '"')
                    {
                        advance();
                        const std::string_view lexeme = input_.substr(start, pos_ - start);
                        tokens.push_back(Token{.kind = TokenKind::StringLiteral,
                                               .lexeme = lexeme,
                                               .span = {start, pos_}});
                        break;
                    }

                    if (ch == '\n' || ch == '\r')
                    {
                        return make_error(start, pos_, "unterminated string literal");
                    }

                    if (ch == '\\')
                    {
                        // Escape sequence: consume backslash + one char if present.
                        advance();
                        if (is_at_end())
                        {
                            return make_error(start, pos_, "unterminated string literal");
                        }
                        advance();
                        continue;
                    }

                    advance();
                }

                if (is_at_end())
                {
                    return make_error(start, pos_, "unterminated string literal");
                }

                continue;
            }

            // Two-character operators
            if (c == '-' && next == '>')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::Arrow, start, pos_));
                continue;
            }

            if (c == '=' && next == '=')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::EqualEqual, start, pos_));
                continue;
            }

            if (c == '!' && next == '=')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::BangEqual, start, pos_));
                continue;
            }

            if (c == '<' && next == '=')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::LessEqual, start, pos_));
                continue;
            }

            if (c == '>' && next == '=')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::GreaterEqual, start, pos_));
                continue;
            }

            if (c == '&' && next == '&')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::AndAnd, start, pos_));
                continue;
            }

            if (c == '|' && next == '|')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::OrOr, start, pos_));
                continue;
            }

            if (c == ':' && next == ':')
            {
                pos_ += 2;
                tokens.push_back(make_token(TokenKind::ColonColon, start, pos_));
                continue;
            }

            // Single-character tokens
            advance();
            switch (c)
            {
            case '(':
                tokens.push_back(make_token(TokenKind::LParen, start, pos_));
                break;
            case ')':
                tokens.push_back(make_token(TokenKind::RParen, start, pos_));
                break;
            case '{':
                tokens.push_back(make_token(TokenKind::LBrace, start, pos_));
                break;
            case '}':
                tokens.push_back(make_token(TokenKind::RBrace, start, pos_));
                break;
            case '[':
                tokens.push_back(make_token(TokenKind::LBracket, start, pos_));
                break;
            case ']':
                tokens.push_back(make_token(TokenKind::RBracket, start, pos_));
                break;
            case ';':
                tokens.push_back(make_token(TokenKind::Semicolon, start, pos_));
                break;
            case ',':
                tokens.push_back(make_token(TokenKind::Comma, start, pos_));
                break;
            case ':':
                tokens.push_back(make_token(TokenKind::Colon, start, pos_));
                break;
            case '.':
                tokens.push_back(make_token(TokenKind::Dot, start, pos_));
                break;
            case '+':
                tokens.push_back(make_token(TokenKind::Plus, start, pos_));
                break;
            case '-':
                tokens.push_back(make_token(TokenKind::Minus, start, pos_));
                break;
            case '*':
                tokens.push_back(make_token(TokenKind::Star, start, pos_));
                break;
            case '/':
                tokens.push_back(make_token(TokenKind::Slash, start, pos_));
                break;
            case '=':
                tokens.push_back(make_token(TokenKind::Equal, start, pos_));
                break;
            case '!':
                tokens.push_back(make_token(TokenKind::Bang, start, pos_));
                break;
            case '<':
                tokens.push_back(make_token(TokenKind::Less, start, pos_));
                break;
            case '>':
                tokens.push_back(make_token(TokenKind::Greater, start, pos_));
                break;
            default:
                return make_error(start, pos_, "invalid character");
            }
        }
    }

  private:
    std::string_view input_;
    std::size_t pos_ = 0;

    [[nodiscard]] bool is_at_end() const { return pos_ >= input_.size(); }
    [[nodiscard]] char peek() const;

    [[nodiscard]] char peek_next() const
    {
        const std::size_t n = pos_ + 1;
        return (n < input_.size()) ? input_[n] : '\0';
    }

    void advance() { ++pos_; }

    static bool is_ident_start(char c)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        return (std::isalpha(uc) != 0) || c == '_';
    }

    static bool is_ident_continue(char c)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        return (std::isalnum(uc) != 0) || c == '_';
    }

    static TokenKind keyword_or_ident(std::string_view lexeme)
    {
        if (lexeme == "fn")
        {
            return TokenKind::KwFn;
        }
        if (lexeme == "let")
        {
            return TokenKind::KwLet;
        }
        if (lexeme == "if")
        {
            return TokenKind::KwIf;
        }
        if (lexeme == "else")
        {
            return TokenKind::KwElse;
        }
        if (lexeme == "while")
        {
            return TokenKind::KwWhile;
        }
        if (lexeme == "return")
        {
            return TokenKind::KwReturn;
        }
        if (lexeme == "true")
        {
            return TokenKind::KwTrue;
        }
        if (lexeme == "false")
        {
            return TokenKind::KwFalse;
        }
        if (lexeme == "requires")
        {
            return TokenKind::KwRequires;
        }
        if (lexeme == "ensures")
        {
            return TokenKind::KwEnsures;
        }
        if (lexeme == "where")
        {
            return TokenKind::KwWhere;
        }
        if (lexeme == "unsafe")
        {
            return TokenKind::KwUnsafe;
        }
        if (lexeme == "cap")
        {
            return TokenKind::KwCap;
        }
        if (lexeme == "import")
        {
            return TokenKind::KwImport;
        }
        if (lexeme == "as")
        {
            return TokenKind::KwAs;
        }
        if (lexeme == "struct")
        {
            return TokenKind::KwStruct;
        }
        if (lexeme == "enum")
        {
            return TokenKind::KwEnum;
        }
        return TokenKind::Identifier;
    }

    [[nodiscard]] Token make_token(TokenKind kind, std::size_t start, std::size_t end) const
    {
        return Token{
            .kind = kind, .lexeme = input_.substr(start, end - start), .span = {start, end}};
    }

    [[nodiscard]] curlee::diag::Diagnostic make_error(std::size_t start, std::size_t end,
                                                      std::string_view message) const
    {
        curlee::diag::Diagnostic d;
        d.severity = curlee::diag::Severity::Error;
        d.message = std::string(message);
        d.span = curlee::source::Span{start, end};
        return d;
    } // GCOVR_EXCL_LINE

    // Skips whitespace and comments. Returns a diagnostic on unterminated block comment.
    [[nodiscard]] std::optional<curlee::diag::Diagnostic> skip_trivia()
    {
        while (!is_at_end())
        {
            const char c = peek();

            // whitespace
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                advance();
                continue;
            }

            // line comment
            if (c == '/' && (pos_ + 1) < input_.size() && input_[pos_ + 1] == '/')
            {
                pos_ += 2;
                while (!is_at_end() && peek() != '\n')
                {
                    advance();
                }
                continue;
            }

            // block comment
            if (c == '/' && (pos_ + 1) < input_.size() && input_[pos_ + 1] == '*')
            {
                const std::size_t start = pos_;
                pos_ += 2;
                while (!is_at_end())
                {
                    if (peek() == '*' && (pos_ + 1) < input_.size() && input_[pos_ + 1] == '/')
                    {
                        pos_ += 2;
                        break;
                    }
                    advance();
                }
                if (is_at_end())
                {
                    return make_error(start, pos_, "unterminated block comment");
                }
                continue;
            }

            break;
        }

        return std::nullopt;
    }
};

[[nodiscard]] char Lexer::peek() const
{
    return input_[pos_];
}

} // namespace

LexResult lex(std::string_view input)
{
    return Lexer(input).lex_all();
}

} // namespace curlee::lexer
