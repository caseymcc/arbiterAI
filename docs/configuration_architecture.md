# Dynamic Configuration Management Architecture

## Overview
The Dynamic Configuration Management system provides a flexible way to manage model configurations, including:
- Model download URLs and verification checksums
- Local cache paths and management
- Version compatibility rules
- Remote configuration repositories

## Configuration Schema

### Model Configuration
```json
{
  "name": "string",
  "version": "semver",
  "provider": "string",
  "download_url": "string",
  "file_hash": "string",
  "cache_path": "string",
  "compatibility": {
    "min_client_version": "semver",
    "max_client_version": "semver",
    "supported_platforms": ["string"]
  },
  "metadata": {
    "key": "value"
  }
}
```

### Remote Configuration Source
```json
{
  "repo_url": "string",
  "branch": "string",
  "config_path": "string",
  "auth_token": "string"
}
```

## Repository Structure

Remote configuration repositories should follow this structure:
```
repo-root/
├── versions/
│   ├── v1.0.0/
│   │   ├── models/
│   │   │   ├── provider1.json
│   │   │   ├── provider2.json
│   ├── v1.1.0/
│   │   ├── models/
│   │   │   ├── provider1.json
├── index.json
```

The `index.json` contains version compatibility information:
```json
{
  "versions": {
    "v1.0.0": {
      "min_client_version": "1.0.0",
      "max_client_version": "1.2.0"
    },
    "v1.1.0": {
      "min_client_version": "1.1.0",
      "max_client_version": null
    }
  }
}
```

## Versioning Strategy

### Semantic Versioning
- **MAJOR**: Breaking changes, requires client updates
- **MINOR**: Backward-compatible feature additions
- **PATCH**: Backward-compatible bug fixes

### Compatibility Rules
1. Clients can use any config version where:
   - Client version >= config's `min_client_version`
   - Client version <= config's `max_client_version` (if specified)
2. Clients should prefer the highest compatible config version

## Implementation Details

### Configuration Loading Process
1. Load base configurations from `configs/defaults/models/`
2. Merge with remote configurations (if available)
3. Validate version compatibility
4. Verify file checksums for downloaded models
5. Cache validated configurations locally

### Cache Management
- Models are cached at paths specified in configuration
- LRU eviction policy for cache management
- Checksum verification before using cached models

### Error Handling
- Invalid configurations are logged and skipped
- Failed downloads are retried with exponential backoff
- Compatibility violations trigger fallback to last known good config