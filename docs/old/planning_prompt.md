Create a detailed docs/project_overview.md based on an existing codebase, and set up the project's foundational rules, plans, and decision-tracking systems based on the information in docs/ai_agent_project_management.md.

1. Search the current project directory for any existing documentation or configuration files (like README.md, package.json, requirements.txt, .gitignore, docker-compose.yml, etc.) to infer the project's goals, technical stack, and existing conventions.
   - Read the source code to understand its structure, architectural patterns, and any implicit rules.
2. Ask the user a series of questions to gather additional context and validate the findings, such as:
   - What is the primary business goal of this project?
   - Are there any features explicitly "out of scope" that we should document?
   - Are there specific performance, security, or deployment constraints?
   - What is the preferred architectural pattern (e.g., Hexagonal Architecture, MVC, etc.)?
   - Any additional information not supplied.
  
   Based on this information, I will perform the following steps:
   1. Generate project_overview.md: Create a comprehensive overview document detailing the project's executive summary, goals, scope, technical stack, and constraints.
   2. Create .roo/rules: Establish a set of foundational .roo/rules to enforce coding standards, error handling, and testing requirements across all AI agents.
   3. Draft development_plan.md: Propose an initial development_plan.md with a high-level overview of the project's phases or initial tasks.
   4. Set up .roo/adr: Create the directory and a template for Architecture Decision Records to ensure future technical decisions are properly documented.

The final output will be a set of structured documents and rules that will serve as the foundation for the project and enable the Orchestrator to manage it autonomously.