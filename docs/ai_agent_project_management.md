# Guide: Setting Up and Managing Projects with Roo Code Agents

This guide provides a comprehensive framework for using Roo Code's Orchestrator mode to manage software development projects from inception to completion. By following these steps, you can establish a robust system that ensures project goal consistency, maintains a dynamic roadmap, and preserves the rationale behind key technical decisions, preventing "context drift" and ensuring your AI agents work efficiently and effectively.

The core strategy centers on creating and maintaining a set of structured documents and rules:
- A comprehensive **`project_overview.md`** to define the project's foundation.
- A dynamic **`development_plan.md`** to track tasks and progress.
- A structured **Architecture Decision Record (ADR)** system within the `.roo/adr/` directory to log key decisions.
- A set of **`.roo/rules`** and custom modes to guide the AI's behavior.

Adopting these practices will give your AI-assisted project greater autonomy, consistency, and auditability, leading to more reliable software delivery.

---

## The Orchestrator-Worker Model

This framework is built on the **Orchestrator-Worker architecture**. The Roo Code **Orchestrator mode** acts as the project manager. Its primary responsibility is to break down high-level goals into smaller subtasks and delegate them to specialized "worker" agents (like Code, Architect, or Debug modes) using the `new_task` tool [5].

Crucially, the Orchestrator **only manages the project and creates documentation**; it never writes production code directly [5]. This separation of concerns is vital for maintaining project integrity and ensuring the development process is scalable and maintainable.

---

## Step 1: Create a Comprehensive Project Overview

The first step is to create a `project_overview.md` file at the root of your project. This document is the single source of truth for the project's foundational context. A well-structured overview prevents the AI from getting confused and ensures it remains aligned with the project's goals.

To be effective, your `project_overview.md` must be meticulously structured.

### Define Project Goals, Scope, and Features

Clearly articulate the business goals, the problem to be solved, and the project's scope. Explicitly define what is **in** and **out** of scope to prevent feature creep.

***`project_overview.md` Example:***
```markdown
# Project: [Project Name]

## 1. Executive Summary

- **Problem Statement:** ...
- **Solution Overview:** ...
- **Primary Objective:** ...

## 2. Project Goals

- **Goal 1:** ...
- **Goal 2:** ...

## 3. Scope

- **In Scope:**
  - **Feature Area 1:**
    - Feature 1.1
    - Feature 1.2
  - **Feature Area 2:** ...
- **Out of Scope:** 
  - [Explicitly state what will *not* be covered.]
```

### Specify the Technical Stack and Immutable Requirements

This section defines the project's technical constraints. List the technology stack, versions, libraries, and any mandatory coding standards or architectural patterns. These "immutable requirements" act as high-level guardrails for the AI.

***`project_overview.md` Example:***
```markdown
## 4. Technical Stack & Immutable Requirements

- **Primary Language:** Python 3.10+
- **Web Framework:** FastAPI 0.100+
- **Database:** PostgreSQL 14+
- **ORM:** SQLAlchemy 2.0+
- **Authentication:** OAuth2 with JWT (using `python-jose` library)
- **Testing Framework:** Pytest
- **Architectural Pattern:** Hexagonal Architecture (Ports & Adapters)
- **Coding Standards:** PEP 8 compliance, type hinting required for all functions.
- **Required Libraries:** `uvicorn`, `pydantic`, `alembic`, `passlib`.
```

### Document Constraints and Dependencies

List any external system integrations, performance targets, security requirements, or deployment environment constraints.

***`project_overview.md` Example:***
```markdown
## 5. Constraints & Dependencies

- **Performance:** API response times < 100ms for 90% of requests.
- **Security:** OWASP Top 10 mitigation, PII data must be encrypted at rest.
- **Deployment Environment:** Dockerized, Kubernetes-compatible.
- **External APIs:** Integration with PaymentGatewayAPI v2 (API Key required).
- **Existing Systems:** Must integrate with LegacyUserManagementService via REST.
```

---

## Step 2: Establish Project-Wide Rules

Use the `.roo/rules/` directory to define global, workspace-wide instructions that apply to all Roo Code modes. These rules enforce consistent behavior across the project. Create Markdown files with numerical prefixes to control their loading order (e.g., `00-project-standards.md`).

### Enforce Coding Standards and Architecture

Create rules that operationalize the "Immutable Requirements" from your `project_overview.md`.

***Example `.roo/rules/00-project-standards.md`:***
```markdown
## id: project-standards title: Global Project Coding Standards scope: workspace target_audience: all-agents status: active

### 1. Code Quality

- All new code MUST adhere to PEP 8 style guidelines.
- All functions and methods MUST include type hints.
- Docstrings MUST be provided for all public functions, classes, and modules.

### 2. Error Handling

- All API endpoints MUST implement robust error handling, returning standardized JSON error responses.
- Log critical errors using the configured logging system.

### 3. Testing

- Every new feature MUST be accompanied by corresponding unit and integration tests.
- Tests MUST achieve at least 80% line coverage.
```

### Define an Initialization Workflow

Create a rule to guide the Orchestrator's behavior when it starts a new task. This ensures it always has the latest context.

***Example `.roo/rules/05-initialization-workflow.md`:***
```markdown
## id: initialization-workflow title: Project Initialization Workflow scope: workspace target_audience: Orchestrator status: active

### 1. Task Initiation Protocol

Upon receiving a new high-level task, the Orchestrator MUST first:

1. Read and internalize the `project_overview.md` file.
2. Confirm the latest `development_plan.md` is loaded and understood.
3. Review any existing Architecture Decision Records (ADRs) relevant to the current task.
```

---

## Step 3: Create a Dynamic Development Plan

The `development_plan.md` file is the adaptive, AI-manageable roadmap. The Orchestrator interacts with this file to understand the project's state, track progress, and delegate tasks.

### Structure Tasks with Granular Detail

Each task in the plan should include a status, assignee, description, and explicit acceptance criteria. This makes it unequivocally clear what "done" means.

***`development_plan.md` Example Task:***
```markdown
## 2. Current Sprint: User Authentication Module (Sprint 3)

### Task 3.1: Implement User Registration Endpoint

- **Status:** In Progress
- **Assigned To:** Intern Mode
- **Description:** Develop a `/register` API endpoint that allows new users to create an account.
- **Acceptance Criteria:**
  - Endpoint: `POST /api/v1/users/register`
  - Request Body: `{"email": "user@example.com", "password": "securepassword"}`
  - Response (Success 201): `{"message": "User registered successfully", "user_id": "uuid"}`
  - Password MUST be hashed using bcrypt before storage.
  - A new unit test for the endpoint MUST be created and pass.
- **Dependencies:** Database schema for `users` table must be defined.
- **Rationale Link:** `[.roo/adr/0001-choose-auth-strategy.md]`
```

### Manage Progress

The Orchestrator is responsible for keeping the `development_plan.md` up-to-date. It will update task statuses (e.g., "In Progress", "Completed", "Blocked") as worker agents report back, turning the plan into a real-time project status board.

---

## Step 4: Track Decisions with Architecture Decision Records (ADRs)

To prevent the AI from reversing decisions, use **Architecture Decision Records (ADRs)** to capture the "why" behind significant technical choices. Store them as Markdown files in a `.roo/adr/` directory.

### ADR Directory and Naming

Keep ADRs in `.roo/adr/` at the project root. Use a sequential naming convention like `0001-choose-authentication-strategy.md`.

***Directory Structure:***
```
.
├── .roo/
│   ├── adr/
│   │   ├── 0001-choose-auth-strategy.md
│   │   └── ...
│   └── rules/
├── project_overview.md
├── development_plan.md
└── ... (codebase)
```

### Use a Standard ADR Template

A consistent template ensures all necessary information is captured.

| Field | Description |
| :--- | :--- |
| **ID** | Unique sequential identifier (e.g., `0001`). |
| **Title** | Concise title of the decision. |
| **Status** | `Approved`, `Superseded`, `Deprecated`, `Draft`. |
| **Date** | Date the ADR was created. |
| **Context** | The problem or challenge that necessitated this decision. |
| **Decision** | The selected solution and the detailed rationale for choosing it. |
| **Consequences** | Expected benefits and known drawbacks or trade-offs. |
| **Options Considered**| A list of all alternatives that were evaluated. |
| **Related ADRs** | Links to other relevant ADRs. |

---

## Step 5: Configure and Delegate Tasks

With the framework in place, the Orchestrator can effectively manage the project.

### Task Delegation with `context.md`

When the Orchestrator delegates work using the `new_task` tool, it should generate a temporary `context.md` file for the worker agent. This file contains a curated subset of information relevant only to that specific task, including:
- The task description and acceptance criteria.
- A summary of relevant points from `project_overview.md`.
- Links to relevant files or code snippets.
- Links to any relevant ADRs.

This practice prevents "context drift" and helps the worker agent stay focused.

### Using Specialized Agents

The Orchestrator should choose the most appropriate agent for each task. You can define custom modes in your `.roomodes` file to create specialized agents (e.g., a "ProjectManager" to detail features, a "Documenter" to write documentation, or a "Tester" to write tests).

***Example `.roomodes` for a "ProjectManager" agent:***
```yaml
customModes:
  - slug: project-manager
    name: 🧑‍🏫 Project Manager
    roleDefinition: >-
      You are an expert project manager. Your primary role is to break down
      high-level features into detailed, actionable tasks with clear acceptance
      criteria. You will update the development_plan.md file.
    customInstructions: |
      Break down features by providing a technical and business case explanation.
      For each feature, define clear acceptance criteria.
      Your output MUST be in Markdown and update the development_plan.md file.
    groups: ["read", "edit"]
    source: "project"
```

By meticulously implementing these strategies, you can transform Roo Code into a powerful and reliable partner for autonomous, end-to-end software project completion.
