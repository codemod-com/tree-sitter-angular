/**
 * @file Angular Grammar for tree-sitter
 * @author Dennis van den Berg <dennis@vdberg.dev>
 * @license MIT
 */

/* eslint-disable-next-line spaced-comment */
/// <reference types="tree-sitter-cli/dsl" />

const HTML = require('tree-sitter-html/grammar');

const PREC = {
  CALL: 1,
  ALIAS: 2,
  PIPE: 3,
  NULLISH: 4,
  INTERPOLATION: 5,
};

module.exports = grammar(HTML, {
  name: 'angular',

  externals: ($, original) =>
    original.concat([
      $._interpolation_start,
      $._interpolation_end,
      // $._control_flow_start,
      $.if_start,
      $.else_start,
      $.for_start,
      $.switch_start,
      $.case_start,
      $.default_start,
      $.defer_start,
      $.let_start,
      $.empty_start,
      $.placeholder_start,
      $.loading_start,
      $.error_start,
      $.else_if_start,
      $.at_sign,
    ]),

  rules: {
    // ---------- Root ---------
    _node: ($, original) =>
      choice(
        prec(1, $.icu_expression),
        prec(1, $.interpolation),
        prec(1, $._any_statement),
        $.at_sign,
        original,
      ),

    // ---------- Overrides ----------
    attribute_name: (_) => /[^<>\*.\[\]\(\)"'=\s]+/,
    text: (_) => /[^<>{}&@\s]([^<>{}&@]*[^<>{}&@\s])?/,

    // ----------- Statement block --------
    statement_block: ($) => prec.right(seq('{', repeat($._node), '}')),

    // ---------- Statements ----------
    _any_statement: ($) =>
      choice(
        $.if_statement,
        $.for_statement,
        $.defer_statement,
        $.switch_statement,
        $.let_statement,
        $._alternative_statement,
      ),

    _alternative_statement: ($) =>
      choice(
        field('alternative', $.else_statement),
        field('alternative_condition', $.else_if_statement),
        field('empty', $.empty_statement),
        field('placeholder', $.placeholder_statement),
        field('loading', $.loading_statement),
        field('error', $.error_statement),
      ),

    // ---------- Let Statement ----------
    let_statement: ($) =>
      prec.left(seq(alias($.let_start, $.control_keyword), $.assignment_expression, ';')),

    // ---------- Switch Statement ----------

    switch_statement: ($) =>
      prec.right(
        seq(
          alias($.switch_start, $.control_keyword),
          '(',
          field('value', $.expression),
          ')',
          field('body', $.switch_body),
        ),
      ),

    switch_body: ($) =>
      seq('{', repeat1(choice($.case_statement, $.default_statement)), '}'),

    case_statement: ($) =>
      seq(
        alias($.case_start, $.control_keyword),
        '(',
        field('value', $._any_expression),
        ')',
        field('body', $.statement_block),
      ),

    default_statement: ($) =>
      seq(alias($.default_start, $.control_keyword), field('body', $.statement_block)),

    // ---------- Defer Statement ----------

    defer_statement: ($) =>
      prec.left(
        seq(
          alias($.defer_start, $.control_keyword),
          optional($.defer_trigger),
          field('body', $.statement_block),
        ),
      ),

    placeholder_statement: ($) =>
      prec.left(
        seq(
          alias($.placeholder_start, $.control_keyword),
          optional($.placeholder_minimum),
          field('body', $.statement_block),
        ),
      ),

    loading_statement: ($) =>
      prec.left(
        seq(
          alias($.loading_start, $.control_keyword),
          optional($.loading_condition),
          field('body', $.statement_block),
        ),
      ),

    error_statement: ($) =>
      seq(alias($.error_start, $.control_keyword), field('body', $.statement_block)),

    defer_trigger: ($) =>
      seq(
        '(',
        field('condition', $.defer_trigger_condition),
        optional(repeat(seq(';', field('condition', $.defer_trigger_condition)))),
        ')',
      ),

    placeholder_minimum: ($) => seq('(', field('minimum', $.timed_expression), ')'),

    loading_condition: ($) =>
      seq(
        '(',
        field('condition', $.timed_expression),
        optional(seq(';', field('condition', $.timed_expression))),
        ')',
      ),

    defer_trigger_condition: ($) =>
      seq(
        optional(alias('prefetch', $.prefetch_keyword)),
        choice(
          seq(alias('when', $.special_keyword), field('trigger', $._any_expression)),
          seq(alias('on', $.special_keyword), field('trigger', $._primitive)),
        ),
      ),

    timed_expression: ($) =>
      seq(alias(choice('after', 'minimum'), $.special_keyword), field('value', $.number)),

    // ---------- For Statement ----------
    for_statement: ($) =>
      prec.left(
        seq(
          alias($.for_start, $.control_keyword),
          '(',
          field('declaration', $.for_declaration),
          optional(field('references', $.for_references)),
          ')',
          field('body', $.statement_block),
        ),
      ),

    empty_statement: ($) =>
      seq(alias($.empty_start, $.control_keyword), field('body', $.statement_block)),

    for_declaration: ($) =>
      seq(
        field('name', $.identifier),
        alias('of', $.special_keyword),
        field('value', $.expression),
        ';',
        alias('track', $.special_keyword),
        field('track', $._any_expression),
      ),

    for_references: ($) =>
      seq(
        ';',
        field('reference', $.for_reference),
        repeat(seq(';', field('reference', $.for_reference))),
      ),

    for_reference: ($) =>
      seq(
        alias('let', $.special_keyword),
        field('alias', $.assignment_expression),
        repeat(seq(',', field('alias', $.assignment_expression))),
      ),

    // ---------- If Statement ----------
    if_statement: ($) =>
      prec.right(
        seq(
          alias($.if_start, $.control_keyword),
          '(',
          field('condition', $.if_condition),
          optional(field('reference', $.if_reference)),
          ')',
          field('consequence', $.statement_block),
          optional(repeat($._alternative_statement)),
        ),
      ),

    else_if_statement: ($) =>
      prec.right(
        seq(
          alias($.else_if_start, $.control_keyword),
          '(',
          field('condition', $.if_condition),
          optional(field('reference', $.if_reference)),
          ')',
          field('consequence', $.statement_block),
        ),
      ),

    else_statement: ($) =>
      prec.right(
        seq(alias($.else_start, $.control_keyword), field('body', $.statement_block)),
      ),

    if_condition: ($) => prec.right(PREC.CALL, $._any_expression),
    if_reference: ($) => seq(';', alias('as', $.special_keyword), $.identifier),

    // ---------- Expressions -----------
    _any_expression: ($) =>
      choice(
        $.binary_expression,
        $.unary_expression,
        $.expression,
        $.ternary_expression,
        $.nullish_coalescing_expression,
        prec(3, $.conditional_expression),
      ),

    assignment_expression: ($) =>
      seq(field('name', $.identifier), '=', field('value', $._any_expression)),

    // -------- ICU expressions ---------
    icu_expression: ($) =>
      seq(
        '{',
        choice($._any_expression),
        ',',
        $.icu_clause,
        ',',
        repeat1($.icu_case),
        '}',
      ),

    icu_clause: () => choice('plural', 'select'),

    icu_case: ($) => seq($.icu_category, '{', repeat1($._node), '}'),

    icu_category: () => /[^{}]+/i,

    // ---------- Interpolation ---------
    interpolation: ($) =>
      prec.right(
        PREC.INTERPOLATION,
        seq(
          alias($._interpolation_start, '{{'),
          choice($._any_expression),
          alias($._interpolation_end, '}}'),
        ),
      ),

    // ---------- Property Binding ---------
    attribute: ($) =>
      choice(
        prec(1, $.property_binding),
        prec(1, $.two_way_binding),
        prec(1, $.animation_binding),
        prec(1, $.event_binding),
        prec(1, $.structural_directive),
        $._normal_attribute, // <-- This needs to be hidden from syntax tree
      ),

    // ---------- Structural Directives ---------
    structural_directive: ($) =>
      seq(
        '*',
        $.identifier,
        optional(
          seq(
            '=',
            $._double_quote,
            choice($.structural_declaration, $.structural_expression),
            $._double_quote,
          ),
        ),
      ),

    structural_expression: ($) =>
      seq(
        $._any_expression,
        optional($._alias),
        choice(
          $._structural_let_expression,
          seq(
            optional($._then_template_expression),
            optional($._else_template_expression),
          ),
          optional($._context_expression),
        ),
      ),

    structural_declaration: ($) =>
      seq(
        alias('let', $.special_keyword),
        seq(
          $.structural_assignment,
          repeat(seq(choice(';', ','), $.structural_assignment)),
        ),
      ),

    structural_assignment: ($) =>
      choice(
        seq(field('name', $.identifier), ':', field('value', $.identifier)),
        prec.left(
          PREC.ALIAS,
          seq(
            optional(alias('let', $.special_keyword)),
            field('name', $.identifier),
            optional(
              seq(
                field('operator', choice('=', 'of')),
                field('value', $.expression),
                optional($._alias),
              ),
            ),
          ),
        ),
        seq(field('name', $.identifier), optional($._alias)),
      ),

    _alias: ($) => seq(alias('as', $.special_keyword), field('alias', $.identifier)),
    _then_template_expression: ($) =>
      seq(';', alias('then', $.special_keyword), $.identifier),
    _else_template_expression: ($) =>
      seq(';', alias('else', $.special_keyword), $.identifier),
    _context_expression: ($) =>
      seq(
        ';',
        choice(alias('context', $.special_keyword), field('named', $.identifier)),
        ':',
        $._any_expression,
      ),

    _structural_let_expression: ($) =>
      seq(
        ';',
        alias('let', $.special_keyword),
        field('name', $.identifier),
        optional($._alias),
      ),

    // ---------- Bindings ----------
    property_binding: ($) => seq('[', $.binding_name, ']', $._binding_assignment),
    event_binding: ($) => seq('(', $.binding_name, ')', $._binding_assignment),
    two_way_binding: ($) => seq('[(', $.binding_name, ')]', $._binding_assignment),
    animation_binding: ($) =>
      seq('[@', $.binding_name, ']', optional(field('trigger', $._binding_assignment))),

    _binding_assignment: ($) =>
      seq(
        '=',
        $._double_quote,
        optional(choice($._any_expression, $.assignment_expression)),
        repeat(seq(';', optional(choice($._any_expression, $.assignment_expression)))),
        $._double_quote,
      ),

    binding_name: ($) => seq(choice($.identifier, $.member_expression)),

    _attribute_node: ($) =>
      choice(
        repeat(choice(token.immediate(/[^"]/), $._escape_sequence)),
        prec(1, $.interpolation),
      ),

    _normal_attribute: ($) =>
      seq(
        $.attribute_name,
        optional(
          seq(
            '=',
            choice(
              seq(
                $._double_quote,
                repeat(
                  choice(
                    alias(token.immediate(/[^"{]+/), $.string_content),
                    $._escape_sequence,
                    $.interpolation,
                  ),
                ),
                $._double_quote,
              ),
              seq(
                $._single_quote,
                repeat(
                  choice(
                    alias(token.immediate(/[^'{]+/), $.string_content),
                    $._escape_sequence,
                    $.interpolation,
                  ),
                ),
                $._single_quote,
              ),
            ),
          ),
        ),
      ),

    // ---------- Expressions ---------
    // Expression
    expression: ($) =>
      prec.left(seq($._primitive, optional(field('pipes', $.pipe_sequence)))),

    // Unary expression
    unary_expression: ($) =>
      seq(
        field('operator', alias('!', $.unary_operator)),
        field('value', choice($.expression, $.unary_expression)),
      ),

    // Binary expression
    binary_expression: ($) =>
      prec.left(
        PREC.CALL + 1, // Increase precedence slightly above CALL to ensure nesting
        seq(
          field('left', choice($.expression, $.binary_expression)),
          field('operator', $._binary_op),
          field('right', $.expression),
        ),
      ),

    // Ternary expression
    ternary_expression: ($) =>
      prec.right(
        PREC.CALL,
        seq(
          field('condition', $._any_expression),
          alias('?', $.ternary_operator),
          field('consequence', choice($.group, $._any_expression)),
          alias(':', $.ternary_operator),
          field('alternative', choice($.group, $._any_expression)),
        ),
      ),

    // Nullish coalescing expression
    nullish_coalescing_expression: ($) =>
      prec.right(
        PREC.NULLISH,
        seq(
          field('condition', $._any_expression),
          alias('??', $.coalescing_operator),
          field('default', $._primitive),
        ),
      ),

    // Conditional expression
    conditional_expression: ($) =>
      prec.right(
        PREC.CALL,
        seq(
          field('left', choice($._primitive, $.unary_expression, $.binary_expression)),
          alias(choice('||', '&&'), $.conditional_operator),
          field(
            'right',
            choice(
              $.expression,
              $.unary_expression,
              $.binary_expression,
              $.conditional_expression,
            ),
          ),
        ),
      ),

    // ---------- Pipes ---------
    pipe_sequence: ($) =>
      prec.left(PREC.PIPE, repeat1(seq(alias('|', $.pipe_operator), $.pipe_call))),

    pipe_call: ($) =>
      prec.left(
        PREC.PIPE,
        seq(field('name', $.identifier), optional(field('arguments', $.pipe_arguments))),
      ),

    pipe_arguments: ($) =>
      prec.left(
        PREC.PIPE,
        seq(
          ':',
          field('argument', $._any_expression), // Use _any_expression to allow full expression parsing
          repeat(seq(':', field('argument', $._any_expression))), // Allow multiple arguments
        ),
      ),

    // ---------- Primitives ----------
    _primitive: ($) =>
      choice(
        $.object,
        $.array,
        $.identifier,
        $.string,
        $.number,
        $.group,
        $.call_expression,
        $.member_expression,
        $.bracket_expression,
      ),

    // Object
    object: ($) => seq('{', commaSep($.pair), optional(','), '}'),
    pair: ($) =>
      seq(
        field('key', choice($.identifier, $.string)),
        optional(seq(':', field('value', $._any_expression))),
      ),

    // Array
    array: ($) => seq('[', commaSep($._any_expression), optional(','), ']'),

    // Identifier
    identifier: () => /[a-zA-Z_\$][a-zA-Z_0-9\-\$]*/,

    _escape_sequence: (_) =>
      token.immediate(
        seq(
          '\\',
          choice(
            /[^xu0-7]/,
            /[0-7]{1,3}/,
            /x[0-9a-fA-F]{2}/,
            /u[0-9a-fA-F]{4}/,
            /u\{[0-9a-fA-F]+\}/,
            /[\r?][\n\u2028\u2029]/,
          ),
        ),
      ),

    // String
    string: ($) =>
      choice(
        seq(
          $._double_quote,
          repeat(choice(token.immediate(/[^"]/), $._escape_sequence)),
          $._double_quote,
        ),
        seq(
          $._single_quote,
          repeat(choice(token.immediate(/[^']/), $._escape_sequence)),
          $._single_quote,
        ),
      ),

    // Number
    number_fragment: () => /\-?[0-9]+\.?[0-9]*/,

    number: ($) =>
      choice($.number_fragment, seq($.number_fragment, alias(choice('ms', 's'), $.unit))),

    // Group
    group: ($) => seq('(', $._any_expression, ')'),

    // Call expression
    call_expression: ($) =>
      prec.left(
        PREC.CALL,
        seq(
          field('function', $.identifier),
          '(',
          optional(field('arguments', $.arguments)),
          ')',
        ),
      ),
    arguments: ($) =>
      commaSep1(choice($.expression, $.binary_expression, $.unary_expression)),

    // Member expression
    member_expression: ($) =>
      seq(
        field('object', $._primitive),
        choice(
          seq(
            choice('.', '?.', '!.'),
            choice(field('property', $.identifier), field('call', $.call_expression)),
          ),
        ),
      ),

    // Bracket expression
    bracket_expression: ($) =>
      prec.left(
        PREC.CALL,
        seq(
          field('object', $._primitive),
          optional(choice('?.', '!')),
          '[',
          field(
            'property',
            choice(
              $.identifier,
              $.static_member_expression,
              $.bracket_expression,
              $.member_expression,
            ),
          ),
          ']',
        ),
      ),

    static_member_expression: ($) => $._any_expression,

    // ---------- Base ----------
    _closing_bracket: (_) => token(prec(-1, '}')),
    // eslint-disable-next-line quotes
    _single_quote: () => "'",
    _double_quote: () => '"',
    _binary_op: () =>
      choice(
        '+',
        '-',
        '/',
        '*',
        '%',
        '==',
        '===',
        '!=',
        '!==',
        '&&',
        '||',
        '<',
        '<=',
        '>',
        '>=',
      ),
  },
});

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @return {SeqRule}
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

/**
 * Creates a rule to optionally match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @return {ChoiceRule}
 */
function commaSep(rule) {
  return optional(commaSep1(rule));
}
