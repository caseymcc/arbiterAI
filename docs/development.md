# Development Process

This document outlines the development workflow for `arbiterAI`. We use a file-based task tracking system to manage our work.

## Task Tracking

Tasks are maintained as markdown files in the `tasks/` directory.

### Directory Structure

*   **`tasks/todo/`**: Contains tasks that are planned but not yet started.
*   **`tasks/current/`**: Contains the single task that is currently being worked on.
*   **`tasks/completed/`**: Contains tasks that have been finished.

### Workflow

1.  **Select a Task**: Move a task file from `tasks/todo/` to `tasks/current/`.
2.  **Work**: Perform the necessary coding, testing, and verification.
3.  **Complete**: Move the task file from `tasks/current/` to `tasks/completed/`. Update the task file with notes on what was done.

### Task File Format

Task files should use Markdown and include:

*   **Title**: Clear description of the task.
*   **Description**: Detailed requirements and context.
*   **Acceptance Criteria**: What must be true for the task to be done.
*   **Progress**: Checklists or notes on progress.
