# GitHub Issue Investigation

When a GitHub issue is created, capture the complete issue, comments, and all
linked attachments locally before investigating it. Use the GitHub integration;
use authenticated `gh` when helpful.

Follow the issue 1 structure:

- Main brief: `docs/issues/issue-N-short-slug.md`
- Evidence directory: `docs/issues/issue-N-evidence/`
- Original archives retained in the evidence directory.
- Complete extracted contents below `.../raw/`, including binary and metadata
  files.
- Optional readable text copies, without replacing raw artifacts.

The brief must be agent-ready and include:

- Issue metadata, original report, environment, and discussion.
- Attachment inventory with sizes/hashes where practical and local links.
- Facts, hypotheses, diagnostics, relevant source paths, constraints, and a
  conservative investigation plan.

Preserve all attachments locally; exclude files only later by explicit choice.
Do not publish issue evidence without explicit authorization.
