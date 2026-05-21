/**
 * Conventional Commits 1.0.0 enforcement.
 *
 * Mirrors the Conventional Commits spec referenced in project conventions:
 * type and scope lowercase, imperative description, body wrapped at ~72,
 * blank line between header and body, ASCII only (no emoji or smart quotes).
 */
module.exports = {
  extends: ["@commitlint/config-conventional"],
  rules: {
    "type-enum": [
      2,
      "always",
      [
        "feat",
        "fix",
        "perf",
        "refactor",
        "docs",
        "test",
        "build",
        "ci",
        "chore",
        "style",
        "revert",
      ],
    ],
    "type-case": [2, "always", "lower-case"],
    // Allow sentence-case subjects so dependabot's auto-generated
    // "Bump X from Y to Z" titles pass the gate; still forbid all-caps
    // and pascal-case which are signs of hand-typed shouting.
    "subject-case": [2, "never", ["pascal-case", "upper-case"]],
    "subject-full-stop": [2, "never", "."],
    "header-max-length": [2, "always", 100],
    "body-leading-blank": [2, "always"],
    "footer-leading-blank": [2, "always"],
  },
};
