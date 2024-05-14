#include "json.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

// Opaque backing type for a json node.
struct json {
    // Every json_t has a pointer to the root of its tree.
    json_t* root;

    // Every json_t has a pointer to its parent, or null in the case of the root.
    json_t* parent;

    // A json_t is defined by a node type, and a null-terminated string that starts
    // at the first character that defines it. Note that this string can and will
    // continue to hold other parts of the entire json document.
    json_node_type_t node_type;
    char* backing_data;

    // A json node usually has a path_name:
    //   - The root node has a null path_name.
    //   - A descendant of a JSON_ARRAY has a null path_name because it is addressed by index.
    //   - All other json nodes have the name of the key insides of its parent node that refers to itself.
    char* path_name;

    // Because json is parsed lazily, a json can have three amounts of child nodes:
    //   - A negative amount means that we haven't checked yet.
    //   - A zero amount means this is a leaf node.
    //   - A positive amount means there are child nodes.
    int64_t children_count;
    json_t** children;

    // A child node not of type JSON_NULL, JSON_OBJECT, or JSON_ARRAY also has a value, which will be parsed and
    // populated upon querying it the first time.
    char* value_string;
    double value_double;
    int64_t value_int;
};

// Checks whether or not a given character is a json-defined whitespace.
int p_is_whitespace(char c);

// Trims off the whitespace off of both ends of a passed string.
// If there is anything left after this, makes a copy and returns it.
char* p_trim_whitespace(const char* s, size_t length);

// Returns a pointer to the first non-whitespace character in s or NULL if no such character exists.
char* p_eat_whitespace(char* s);

// Returns a pointer to the first character after the json string pointed at by s or NULL if s does not
// point to a valid json string.
char* p_is_valid_string(char* s);

// Returns a pointer to the first character after the json number pointed at by s or NULL if s does not
// point to a valid json number.
char* p_is_valid_number(char* s);

// Returns a pointer to the first character after the json object or array pointed at by s or NULL if s
// does not point to a valid json declaration of this type.
char* p_is_valid_json(char* s, json_node_type_t t);

// Builds a root json_t and returns it.
json_t* p_build_root_json_node(char* data, json_node_type_t type);

// Builds a child json node and returns it.
json_t* p_build_child_json_node(json_t* parent, char* data, char* path_name, json_node_type_t type);

// Recursively clears the memory owned by a json node.
void p_free_json_node(json_t* j);

// Parses the json in j and fully loads the first layer of children.
void p_lazy_load(json_t* j);

// Recursively traverses j to find the node pointed at by path.
json_t* p_traverse_json(json_t* j, const char* path);

// Unescapes a string in place.
void p_unescape_string(char* s);

int p_is_whitespace(char c) {
    return (c == ' ' || c == '\r' || c == '\n' || c == '\t');
}

char* p_trim_whitespace(const char* s, size_t length) {
    // Nibble off whitespace characters from both ends of s and return NULL if nothing remains.
    const char* it = s;
    const char* rit = s + (length-1);
    while(p_is_whitespace(*it)) {
        it++;
        length--;
        if(length == 0) return NULL;
    }
    while(p_is_whitespace(*rit)) {
        rit--;
        length--;
        if(length == 0) return NULL;
    }

    // Copy the remaining string.
    char* copy = calloc(length + 1, sizeof(char));
    if(copy == NULL) {
        fprintf(stderr, "p_trim_whitespace: fatal error allocating copy of string.\n");
        abort();
        return NULL;
    }
    memcpy(copy, it, length);
    return copy;
}

char* p_eat_whitespace(char* s) {
    if(*s == '\0') return NULL;
    while(p_is_whitespace(*s)) {
        s++;
        if(*s == '\0') return NULL;
    }
    return s;
}

char* p_is_valid_string(char* s) {
    // Strings must start with ".
    if(*s != '"') return NULL;

    while(1) {
        // Eat a character.
        s++;

        // If we ran out of string, it's invalid.
        if(*s == '\0') return NULL;

        // If we find a ", break.
        if(*s == '"') break;

        // If we find a \, check if it's a valid escape sequence and advance the iterator.
        if(*s == '\\') {
            if(s[1] == '\0') return NULL;
            if(s[1] == '\\' || s[1] == '"' || s[1] == '/' || s[1] == 'b' || s[1] == 'f' || s[1] == 'n' || s[1] == 'r' || s[1] == 't') {
                s++;
                continue;
            }
            if(s[1] == 'u') {
                if(s[2] == '\0' || s[3] == '\0' || s[4] == '\0' || s[5] == '\0') return NULL;
                for(int i = 0; i < 4; i++) {
                    s++;
                    if(s[1] >= '0' && s[1] <= '9') continue;
                    if(s[1] >= 'a' && s[1] <= 'f') continue;
                    if(s[1] >= 'A' && s[1] <= 'F') continue;
                    return NULL;
                }
                s++;
                continue;
            }
            return NULL;
        }
    }

    // Jump over the closing ".
    s++;

    // In every valid json document, there has to be something after the end of the string.
    if(*s == '\0') return NULL;

    return s;
}

char* p_is_valid_number(char* s) {
    // A number may begin with a -.
    if(*s == '-') s++;

    // There must be more.
    if(*s == '\0') return NULL;

    // If there's a 0, it must be followed by a '.'.
    if(*s == '0' && s[1] != '.') return NULL;

    // Eat digits.
    while(*s >= '0' && *s <= '9') s++;

    // There must be more.
    if(*s == '\0') return NULL;

    // If there's whitespace or a comma, we're done.
    if(*s == ',' || p_is_whitespace(*s)) return s;

    // Do we have a fractional part?
    if(*s == '.') {
        // Eat digits.
        while(*s >= '0' && *s <= '9') s++;

        // There must be more.
        if(*s == '\0') return NULL;

        // If there's whitespace or a comma, we're done.
        if(*s == ',' || p_is_whitespace(*s)) return s;
    }

    // Do we have an exponent?
    if(*s == 'e' || *s == 'E') {
        // There must be more.
        s++;
        if(*s == '\0') return NULL;

        // +/- is allowed.
        if(*s == '-' || *s == '+') {
            // There must be more.
            s++;
            if(*s == '\0') return NULL;
        }

        // Eat digits.
        while(*s >= '0' && *s <= '9') s++;

        // There must be more.
        if(*s == '\0') return NULL;

        // If there's whitespace or a comma, we're done.
        if(*s == ',' || p_is_whitespace(*s)) return s;
    }

    // We reached an invalid character.
    return NULL;
}

char* p_is_valid_json(char *s, json_node_type_t t) {
    if(t == JSON_OBJECT) {
        // A json object must start with {.
        if(*s != '{') return NULL;

        // We're currently a json object. Skip the { and the whitespace.
        s = p_eat_whitespace(s+1);
        if(s == NULL) return NULL;

        // A JSON object may contain nothing (aka we get to the } immediately), or a series of key:value pairs.
        while(1) {
            if(*s == '}') {
                // End of object, object is thus valid.
                s++;
                return s;
            } else if(*s == '"') {
                // Maybe we have a key!
                s = p_is_valid_string(s);
                if(s == NULL) return NULL;

                // We had a valid key, eat whitespace.
                s = p_eat_whitespace(s);
                if(s == NULL) return NULL;

                // We must have a ':' now.
                if(*s != ':') return NULL;

                // Eat whitespace once again to get to the value.
                s = p_eat_whitespace(s+1);
                if(s == NULL) return NULL;

                // A valid value may be one of the following:
                // - An object.
                // - An array.
                // - A string.
                // - A number.
                // - null, true, or false.
                if(*s == '{') {
                    s = p_is_valid_json(s, JSON_OBJECT);
                } else if(*s == '[') {
                    s = p_is_valid_json(s, JSON_ARRAY);
                } else if(strstr(s, "true") == s || strstr(s, "null") == s) {
                    s += 4;
                } else if(strstr(s, "false") == s) {
                    s += 5;
                } else if(*s == '"') {
                    s = p_is_valid_string(s);
                } else {
                    s = p_is_valid_number(s);
                }

                // Check for validity.
                if(s == NULL) return NULL;

                // We had a valid value, eat whitespace.
                s = p_eat_whitespace(s);
                if(s == NULL) return NULL;

                // Each value in a json object either ends with the end of the json object,
                // or a comma to signify more are coming.
                if(*s == '}') {
                    // End of object.
                    s++;
                    return s;
                } else if(*s == ',') {
                    // Check if there's a next value.
                    s = p_eat_whitespace(s+1);
                    if(s == NULL) return NULL;

                    // Closing the object isn't allowed right now.
                    if(*s == '}') return NULL;

                    // Allow the loop to continue eating the string.
                    continue;
                }

                // If we got here, the json object is malformed.
                return NULL;
            } else {
                // Nothing valid.
                return NULL;
            }
        }
    } else if(t == JSON_ARRAY) {
        // A json array must start with [.
        if(*s != '[') return NULL;

        // We're currently a json array. Skip the [ and the whitespace.
        s = p_eat_whitespace(s+1);
        if(s == NULL) return NULL;

        // A JSON array may contain nothing (aka we get to the ] immediately), or a series of values.
        while(1) {
            if(*s == ']') {
                // End of array, array is thus valid.
                s++;
                return s;
            } else {
                // A valid value in an array may be one of the following:
                // - An object.
                // - An array.
                // - A string.
                // - A number.
                // - null, true, or false.
                if(*s == '{') {
                    s = p_is_valid_json(s, JSON_OBJECT);
                } else if(*s == '[') {
                    s = p_is_valid_json(s, JSON_ARRAY);
                } else if(strstr(s, "true") == s || strstr(s, "null") == s) {
                    s += 4;
                } else if(strstr(s, "false") == s) {
                    s += 5;
                } else if(*s == '"') {
                    s = p_is_valid_string(s);
                } else {
                    s = p_is_valid_number(s);
                }

                // Check for validity.
                if(s == NULL) return NULL;

                // We had a valid value, eat whitespace.
                s = p_eat_whitespace(s);
                if(s == NULL) return NULL;

                // Each value in a json array either ends with the end of the json array,
                // or a comma to signify more are coming.
                if(*s == ']') {
                    // End of array.
                    s++;
                    return s;
                } else if(*s == ',') {
                    // Check if there's a next value.
                    s = p_eat_whitespace(s+1);
                    if(s == NULL) return NULL;

                    // Closing the array isn't allowed right now.
                    if(*s == ']') return NULL;

                    // Allow the loop to continue eating the string.
                    continue;
                }

                // If we got here, the json array is malformed.
                return NULL;
            }
        }
    } else {
        // We shouldn't ever get here.
        return NULL;
    }
}

json_t* p_build_root_json_node(char* data, json_node_type_t type) {
    json_t* j = calloc(1, sizeof(json_t));
    if(j == NULL) {
        fprintf(stderr, "p_build_root_json_node: fatal error allocating json_t.\n");
        abort();
        return NULL;
    }

    j->backing_data = data;
    j->children = NULL;
    j->children_count = -1;
    j->node_type = type;
    j->parent = NULL;
    j->path_name = NULL;
    j->root = j;
    j->value_double = 0.0;
    j->value_int = 0;
    j->value_string = NULL;
    return j;
}

json_t* p_build_child_json_node(json_t* parent, char* data, char* path_name, json_node_type_t type) {
    json_t* j = calloc(1, sizeof(json_t));
    if(j == NULL) {
        fprintf(stderr, "p_build_child_json_node: fatal error allocating json_t.\n");
        abort();
        return NULL;
    }

    j->backing_data = data;
    j->children = NULL;
    j->children_count = -1;
    j->node_type = type;
    j->parent = parent;
    j->path_name = path_name;
    j->root = parent->root;
    j->value_double = 0.0;
    j->value_int = 0;
    j->value_string = NULL;
    return j;
}

void p_free_json_node(json_t* j) {
    // Recurse deeper.
    for(int i = 0; i < j->children_count; i++) {
        p_free_json_node(j->children[i]);
    }

    // Free memory for self.
    if(j->path_name) free(j->path_name);
    if(j->value_string) free(j->value_string);
    if(j->children) free(j->children);
    free(j);
}

void p_lazy_load(json_t* j) {
    // Return if j is null or already loaded.
    if(j == NULL) return;
    if(j->children_count != -1) return;

    // Start with the assumption that j has no children.
    j->children_count = 0;

    if(j->node_type == JSON_NUMBER) {
        // Get the number from the data.
        char* start = j->backing_data;
        char* end = p_is_valid_number(j->backing_data);

        // This shouldn't happen, but...
        if(end == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error reading number.\n");
            abort();
            return;
        }

        end--;

        // Parse the number.
        char* number = calloc((end-start) + 2, sizeof(char));
        if(number == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error allocating number string.\n");
            abort();
            return;
        }
        memcpy(number, start, (end-start) + 1);
        double d = atof(number);
        int64_t i = round(d);
        free(number);

        // Save it.
        j->value_double = d;
        j->value_int = i;
    } else if(j->node_type == JSON_STRING) {
        // Get the string from the data.
        char* start = j->backing_data;
        char* end = p_is_valid_string(j->backing_data);

        // This shouldn't happen, but...
        if(end == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error reading string.\n");
            abort();
            return;
        }

        start += 1;
        end -= 2;

        // Copy the string.
        char* s = calloc((end-start) + 2, sizeof(char));
        if(s == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error allocating string.\n");
            abort();
            return;
        }
        memcpy(s, start, (end-start) + 1);
        p_unescape_string(s);

        // Save it.
        j->value_string = s;
    } else if(j->node_type == JSON_OBJECT) {
        char* start = j->backing_data;
        char* end = p_is_valid_json(j->backing_data, JSON_OBJECT);

        // This shouldn't happen, but...
        if(end == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
            abort();
            return;
        }

        // Skip the opening {.
        start++;
        start = p_eat_whitespace(start);
        if(start == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
            abort();
            return;
        }

        while(start < end) {
            if(*start == '}') {
                return;
            } else if(*start == '"') {
                char* name_start = start + 1;
                start = p_is_valid_string(start);
                char* name_end = start - 2;

                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                    abort();
                    return;
                }

                // Copy the string.
                char* path_name = calloc((name_end-name_start) + 2, sizeof(char));
                if(path_name == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error allocating path name string.\n");
                    abort();
                    return;
                }
                memcpy(path_name, name_start, (name_end-name_start) + 1);
                p_unescape_string(path_name);

                // We must have a ':' now.
                if(*start != ':') {
                    fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                    abort();
                    return;
                }

                // Eat whitespace once again to get to the value.
                start = p_eat_whitespace(start+1);
                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                    abort();
                    return;
                }

                // Create the child object.
                j->children_count++;
                j->children = realloc(j->children, j->children_count * sizeof(json_t*));
                if(j->children == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error allocating children.\n");
                    abort();
                    return;
                }
                if(*start == '{') {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_OBJECT);
                    start = p_is_valid_json(start, JSON_OBJECT);
                } else if(*start == '[') {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_ARRAY);
                    start = p_is_valid_json(start, JSON_ARRAY);
                } else if(strstr(start, "true") == start) {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_TRUE);
                    start += 4;
                } else if(strstr(start, "null") == start) {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_NULL);
                    start += 4;
                } else if(strstr(start, "false") == start) {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_FALSE);
                    start += 5;
                } else if(*start == '"') {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_STRING);
                    start = p_is_valid_string(start);
                } else {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, path_name, JSON_NUMBER);
                    start = p_is_valid_number(start);
                }

                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                    abort();
                    return;
                }

                start = p_eat_whitespace(start);
                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                    abort();
                    return;
                }

                // Each value in a json object either ends with the end of the json object,
                // or a comma to signify more are coming.
                if(*start == '}') {
                    // End of object.
                    start++;
                    continue;
                } else if(*start == ',') {
                    // Check if there's a next value.
                    start = p_eat_whitespace(start+1);
                    if(start == NULL) {
                        fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                        abort();
                        return;
                    }

                    // Closing the object isn't allowed right now.
                    if(*start == '}') {
                        fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                        abort();
                        return;
                    }

                    // Allow the loop to continue eating the string.
                    continue;
                }
            } else {
                fprintf(stderr, "p_lazy_load: fatal error reading object.\n");
                abort();
                return;
            }
        }
    } else if(j->node_type == JSON_ARRAY) {
        char* start = j->backing_data;
        char* end = p_is_valid_json(j->backing_data, JSON_ARRAY);

        // This shouldn't happen, but...
        if(end == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
            abort();
            return;
        }

        // Skip the opening [.
        start++;
        start = p_eat_whitespace(start);
        if(start == NULL) {
            fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
            abort();
            return;
        }

        while(start < end) {
            if(*start == ']') {
                return;
            } else {
                // Eat whitespace to get to the value.
                start = p_eat_whitespace(start);
                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
                    abort();
                    return;
                }

                // Create the child object.
                j->children_count++;
                j->children = realloc(j->children, j->children_count * sizeof(json_t*));
                if(j->children == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error allocating children.\n");
                    abort();
                    return;
                }
                if(*start == '{') {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_OBJECT);
                    start = p_is_valid_json(start, JSON_OBJECT);
                } else if(*start == '[') {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_ARRAY);
                    start = p_is_valid_json(start, JSON_ARRAY);
                } else if(strstr(start, "true") == start) {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_TRUE);
                    start += 4;
                } else if(strstr(start, "null") == start) {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_NULL);
                    start += 4;
                } else if(strstr(start, "false") == start) {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_FALSE);
                    start += 5;
                } else if(*start == '"') {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_STRING);
                    start = p_is_valid_string(start);
                } else {
                    j->children[(j->children_count) - 1] = p_build_child_json_node(j, start, NULL, JSON_NUMBER);
                    start = p_is_valid_number(start);
                }

                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
                    abort();
                    return;
                }

                start = p_eat_whitespace(start);
                if(start == NULL) {
                    fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
                    abort();
                    return;
                }

                // Each value in a json array either ends with the end of the json array,
                // or a comma to signify more are coming.
                if(*start == ']') {
                    // End of array.
                    start++;
                    continue;
                } else if(*start == ',') {
                    // Check if there's a next value.
                    start = p_eat_whitespace(start+1);
                    if(start == NULL) {
                        fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
                        abort();
                        return;
                    }

                    // Closing the array isn't allowed right now.
                    if(*start == ']') {
                        fprintf(stderr, "p_lazy_load: fatal error reading array.\n");
                        abort();
                        return;
                    }

                    // Allow the loop to continue eating the string.
                    continue;
                }
            }
        }
    }

    return;
}

json_t* p_traverse_json(json_t* j, const char* path) {
    // If there's nothing left in the path, return j.
    if(*path == '\0') return j;

    // If j ended up null, return null.
    if(j == NULL) return NULL;

    // If the path is exactly .., return the parent.
    if(strcmp(path, "..") == 0) return j->parent;

    // If the path begins with ../ or ..[, traverse from the parent.
    if(strstr(path, "../") == path || strstr(path, "..[") == path) return p_traverse_json(j->parent, path + 3);

    // At this point, we are definitely going to traverse deeper, so let's lazy load j.
    if(j->children_count == -1) p_lazy_load(j);

    // If j has no children, the path is invalid.
    if(j->children == 0) return NULL;

    // If j is an object, we expect the path to begin like string/ or string[.
    if(j->node_type == JSON_OBJECT) {
        const char* start = path;
        const char* end = path;
        while(*end != '/' && *end != '[' && *end != '\0') end++;
        end--;

        char* name = calloc((end-start) + 2, sizeof(char));
        if(name == NULL) {
            fprintf(stderr, "p_traverse_json: fatal error allocating name string.\n");
            abort();
            return NULL;
        }
        memcpy(name, start, (end-start) + 1);

        for(int i = 0; i < j->children_count; i++) {
            json_t* c = j->children[i];
            if(c == NULL) return NULL;
            if(strcmp(name, c->path_name) == 0) {
                free(name);
                if(end[1] == '\0') return c;
                else               return p_traverse_json(c, end+2);
            }
        }
    }

    // If j is an array, we expect the path to begin with a number followed by ][, ]/ or ] and the end of the string.
    if(j->node_type == JSON_ARRAY) {
        const char* start = path;
        const char* end = path;
        while(*end != ']' || *end != '\0') end++;
        if(*end == '\0') return NULL;
        if(end[1] != '[' && end[1] != '/' && end[1] != '\0') return NULL;
        end--;

        char* idx_s = calloc((end-start) + 2, sizeof(char));
        if(idx_s == NULL) {
            fprintf(stderr, "p_traverse_json: fatal error allocating idx string.\n");
            abort();
            return NULL;
        }
        memcpy(idx_s, start, (end-start) + 1);
        int idx = atoi(idx_s);
        free(idx_s);

        if(idx >= j->children_count) return NULL;
        json_t* c = j->children[idx];
        if(c == NULL) return NULL;

        if(end[2] == '\0') return c;
        else               return p_traverse_json(c, end+3);
    }

    // If we get here, nothing was found and the path is invalid.
    return NULL;
}

void p_unescape_string(char* s) {
    char* it = s;
    while(*it != '\0') {
        if(*it == '\\') {
            if(it[1] == 'u' && it[2] != '\0' && it[3] != '\0' && it[4] != '\0' && it[5] != '\0') {
                // UTH-8 escape.
                char hex[5] = {it[2], it[3], it[4], it[5], '\0'};
                int code = strtol(hex, NULL, 16);
                printf("hex: %s code: %d\n", hex, code);
                if(code <= 0x7F) {
                    *it = code;
                    it++;

                    // Shift the rest of the string over.
                    char* shift = it + 5;
                    char* shift_end = it;
                    while(*shift != '\0') {
                        *shift_end = *shift;
                        shift++;
                        shift_end++;
                    }
                    *shift_end = '\0';

                    continue;
                } else if(code <= 0x7FF) {
                    *it = 0xC0 | (code >> 6);
                    it++;
                    *it = 0x80 | (code & 0x3F);
                    it++;

                    // Shift the rest of the string over.
                    char* shift = it + 4;
                    char* shift_end = it;
                    while(*shift != '\0') {
                        *shift_end = *shift;
                        shift++;
                        shift_end++;
                    }
                    *shift_end = '\0';

                    continue;
                } else {
                    *it = 0xE0 | (code >> 12);
                    it++;
                    *it = 0x80 | ((code >> 6) & 0x3F);
                    it++;
                    *it = 0x80 | (code & 0x3F);
                    it++;

                    // Shift the rest of the string over.
                    char* shift = it + 3;
                    char* shift_end = it;
                    while(*shift != '\0') {
                        *shift_end = *shift;
                        shift++;
                        shift_end++;
                    }
                    *shift_end = '\0';

                    continue;
                }

                // Note: JSON does not support escape sequences for code points above 0xFFFF.
            } else {
                switch(it[1]) {
                    // Simple escapes.
                    case '\\': *it = '\\'; break;
                    case '"':  *it = '"';  break;
                    case '/':  *it = '/';  break;
                    case 'b':  *it = '\b'; break;
                    case 'f':  *it = '\f'; break;
                    case 'n':  *it = '\n'; break;
                    case 'r':  *it = '\r'; break;
                    case 't':  *it = '\t'; break;
                }

                // Shift the rest of the string over.
                char* shift = it + 1;
                while(*shift != '\0') {
                    *shift = *(shift+1);
                    shift++;
                }
            }
        }

        it++;
    }
}


//////////////////////////////////////
// Public interface implementation. //
//////////////////////////////////////


json_t* json_parse(const char* s, size_t length) {
    // Quick check for length.
    if(length == 0) return NULL;

    // Check for \0 termination.
    if(s[length] != '\0') return NULL;

    // There may not be any other \0's in the string;
    const char* it = s + (length-1);
    while(it != s && *it != '\0') it--;
    if(it != s) return NULL;

    // Trim the whitespace from both ends of the string.
    char *data = p_trim_whitespace(s, length);
    if(data == NULL) return NULL;

    // We're now certain that the string is well-behaved and the library will now forgo explicitly passing the length.
    // At this point, we must have either an [ or an { character. If we do not, the json is invalid.
    if(*data != '{' && *data != '[') {
        free(data);
        return NULL;
    }

    // We know what type this is supposed to be now.
    json_node_type_t type = JSON_OBJECT;
    if(*data == '[') type = JSON_ARRAY;

    // Validate the json.
    if(p_is_valid_json(data, type) == NULL) {
        free(data);
        return NULL;
    }

    // All is good. Return the json!
    return p_build_root_json_node(data, type);
}

int json_free(json_t* j) {
    // Only free up non-null root nodes.
    if(j == NULL) return 0;
    if(j->root != j) return 0;

    // Recurse over all children.
    for(int i = 0; i < j->children_count; i++) {
        p_free_json_node(j->children[i]);
    }

    // Free up memory.
    if(j->children) free(j->children);
    free(j->backing_data);
    free(j);
    return 1;
}

json_t* json_traverse(json_t* j, const char* path) {
    if(j == NULL) return NULL;

    // A path that begins with / retargets to the root.
    if(*path == '/') {
        j = j->root;
        path++;
    }

    return p_traverse_json(j, path);
}

const char* json_value_as_string(json_t* j) {
    if(j == NULL || j->node_type != JSON_STRING) return NULL;
    if(j->children_count == -1) p_lazy_load(j);
    return j->value_string;
}

double json_value_as_double(json_t* j) {
    if(j == NULL || j->node_type != JSON_NUMBER) return NAN;
    if(j->children_count == -1) p_lazy_load(j);
    return j->value_double;
}

int64_t json_value_as_int(json_t* j) {
    if(j == NULL) return 0;
    if(j->node_type == JSON_TRUE) return 1;
    if(j->node_type != JSON_NUMBER) return 0;
    if(j->children_count == -1) p_lazy_load(j);
    return j->value_int;
}

int json_children_count(json_t* j) {
    if(j == NULL) return 0;
    if(j->children_count == -1) p_lazy_load(j);
    return j->children_count;
}

json_t* json_get_child(json_t* j, int n) {
    if(j == NULL) return NULL;
    if(j->children_count == -1) p_lazy_load(j);
    if(n < 0 || n >= j->children_count) return NULL;
    return j->children[n];
}

const char* json_get_name(json_t* j) {
    if(j == NULL) return "";
    return j->path_name;
}

const char* json_get_data(json_t* j) {
    if(j == NULL) return "";
    return j->root->backing_data;
}

json_node_type_t json_get_type(json_t* j) {
    if(j == NULL) return JSON_INVALID;
    return j->node_type;
}
