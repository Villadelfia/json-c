#ifndef _JSON_H
#define _JSON_H

#include <stddef.h>
#include <stdint.h>

// Possible types of a json node.
typedef enum {
    // Uninitialized or invalid.
    JSON_INVALID = 0,

    // Non-leaf nodes.
    JSON_OBJECT,
    JSON_ARRAY,

    // Leaf nodes.
    JSON_TRUE,
    JSON_FALSE,
    JSON_NULL,
    JSON_NUMBER,
    JSON_STRING
} json_object_type_t;

// json_t is the main opaque type representing a json node.
typedef struct json json_t;

/* Attempts to parse a string of ASCII bytes as json.
 *
 * You must provide a null-terminated string, and the length of that string.
 *
 * If the string is valid json, a json_t* object will
 * be returned, if not, NULL will be returned.
 *
 * The returned object must be released by calling free_json
 * once you no longer need it.
 *
 * A copy is made of the passed string.
 **/
json_t* json_parse(const char* s, size_t length);

/* Frees up the memory of a root json_t*.
 *
 * Will return a zero and do nothing if:
 *   - The json_t* is null.
 *   - The json_t* is not the root node of its tree.
 * Otherwise a non-zero value is returned.
 *
 * This function will invalidate the root node and all its descendants.
 **/
int json_free(json_t* j);

/* Will traverse the json tree of j starting from that node in the tree.
 *
 * If the path specification leads to a valid node, it will get returned, otherwise NULL gets returned.
 *
 * Starting a path specification with / will start traversing the tree from the root, otherwise all
 * traversal is relative.
 *
 * Examples:
 *   - /details/prices[2] -> Starting from the root node, get the details node, then get the prices array and return the third element.
 *   - ../name -> Return the name node of the parent node.
 *   - details/descriptions[0] -> Get the details node inside of this node, get the descriptions array and return the first element.
 *   - model[2]/details -> Get the model array, get the third element, and return the details node within.
 **/
json_t* json_traverse(json_t* j, const char* path);

/* Returns the string value of the leaf node j. If j is of any type other than JSON_STRING, NULL is returned.
 **/
const char* json_value_as_string(json_t* j);

/* Returns the double value of the leaf node j. If j is of any type other than JSON_NUMBER, NaN is returned.
 **/
double json_value_as_double(json_t* j);

/* Returns the integer value of the leaf node j.
 *
 * If j is of type JSON_NUMBER, this will be the nearest integer.
 * If j is of type JSON_TRUE, this will return 1.
 * If j is of type JSON_FALSE, this will return 0.
 *
 * In all other cases this will return 0.
 **/
int64_t json_value_as_int(json_t* j);

/* Gets the amount of child nodes this node has. Generally used for arrays to ease looping.
 **/
int json_children_count(json_t* j);

/* Gets the nth child node of this node. Returns NULL if no such child exists.
 **/
json_t* json_get_child(json_t* j, int n);

/* Gets the name of a json node. All nodes have a name except for the root node and elements of arrays.
 **/
const char* json_get_name(json_t* j);

/* Gets the raw backing data of the json tree.
 **/
const char* json_get_data(json_t* j);

/* Gets the type of the json node.
 **/
json_object_type_t json_get_type(json_t* j);

#endif
