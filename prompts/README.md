# Host Prompts

Host-owned, customized runtime prompts live here. They take precedence over the
framework defaults in `agentic-pipelines/prompts/`.

Create worker / reviewer / repair prompts for this host's pipeline via
`agentic-pipelines/playbooks/how_to_build_pipeline_prompts.md`, once the host
pipeline (`pipeline.yaml`) is designed. Do not copy framework defaults into this
directory; reference them by ID and override only what this host changes.