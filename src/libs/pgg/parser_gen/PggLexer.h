
// Generated from src/libs/pgg/grammar/Pgg.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  PggLexer : public antlr4::Lexer {
public:
  enum {
    DEF = 1, EXPECT = 2, ENSURE = 3, TAP = 4, IMPORT = 5, AS = 6, REPEAT = 7, 
    FOREACH = 8, IN = 9, PARAM = 10, OUTPUT = 11, NONE = 12, TRUE = 13, 
    FALSE = 14, TRIPLE_STRING = 15, STRING = 16, NUMBER = 17, ARROW = 18, 
    LTE = 19, GTE = 20, EQ = 21, NEQ = 22, LT = 23, GT = 24, PLUS = 25, 
    MINUS = 26, STAR = 27, SLASH = 28, PERCENT = 29, BANG = 30, PIPE = 31, 
    AMP = 32, AT = 33, QUESTION = 34, COLON = 35, DOT = 36, COMMA = 37, 
    ASSIGN = 38, LPAREN = 39, RPAREN = 40, LBRACE = 41, RBRACE = 42, LBRACKET = 43, 
    RBRACKET = 44, IDENT = 45, NEWLINE = 46, COMMENT = 47, WS = 48
  };

  explicit PggLexer(antlr4::CharStream *input);

  ~PggLexer() override;


  public:
      // Open brackets, innermost last: '(' / '[' make newlines insignificant
      // (multi-line calls and lists, v1.33); '{' (bodies, enum types) restores them.
      std::vector<char> brackets;
      std::vector<size_t> bracketLines;

      void openBracket(char c) {
          brackets.push_back(c);
          bracketLines.push_back(getLine());
      }
      void closeBracket(char open) {
          while (!brackets.empty()) {
              const char top = brackets.back();
              brackets.pop_back();
              bracketLines.pop_back();
              if (top == open) break;
          }
      }
      bool insideParens() const {
          return !brackets.empty() && brackets.back() != '{';
      }


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  void action(antlr4::RuleContext *context, size_t ruleIndex, size_t actionIndex) override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.
  void LPARENAction(antlr4::RuleContext *context, size_t actionIndex);
  void RPARENAction(antlr4::RuleContext *context, size_t actionIndex);
  void LBRACEAction(antlr4::RuleContext *context, size_t actionIndex);
  void RBRACEAction(antlr4::RuleContext *context, size_t actionIndex);
  void LBRACKETAction(antlr4::RuleContext *context, size_t actionIndex);
  void RBRACKETAction(antlr4::RuleContext *context, size_t actionIndex);
  void NEWLINEAction(antlr4::RuleContext *context, size_t actionIndex);

  // Individual semantic predicate functions triggered by sempred() above.

};

