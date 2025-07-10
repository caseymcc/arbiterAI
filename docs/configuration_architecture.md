# Dynamic Configuration Management Architecture (v2)

## Overview
The Dynamic Configuration Management system provides a flexible way to manage model configurations, including:
- Model download URLs and verification checksums
- Local cache paths and management
- Version compatibility rules
- Remote configuration repositories
- Model ranking and preference system
- Schema validation

## Version 2.0 Changes
- Added formal JSON schema validation
- Introduced model ranking system
- Standardized download metadata format
- Added explicit schema versioning
- Improved version compatibility checking


## Overview
The Dynamic Configuration Management system provides a flexible way to manage model configurations, including:
- Model download URLs and verification checksums
- Local cache paths and management
- Version compatibility rules
- Remote configuration repositories

## Configuration Schema

### Model Configuration (v2 Format)
```json
{
  "schema_version": "2.0",
  "models": [
    {
      "model": "string",
      "provider": "string",
      "version": "semver",
      "ranking": 0,
      "download": {
        "url": "string",
        "sha256": "string",
        "cachePath": "string"
      },
      "compatibility": {
        "min_client_version": "semver",
        "max_client_version": "semver",
        "supported_platforms": ["string"]
      }
    }
  ]
}
```

### Backward Compatibility
Version 1.x configurations are automatically converted to v2 format:
- `name` → `model`
- `download_url` → `download.url`
- `file_hash` → `download.sha256`
- `cache_path` → `download.cachePath`
- Default `ranking` of 0 applied
- Default `schema_version` of "2.0" applied

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

## Schema Validation
All configurations must validate against `schemas/model_config.schema.json`.

Validation checks:
1. Required fields present
2. Field types correct
3. Version format valid
4. Download URLs properly formatted
5. SHA256 checksums valid

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