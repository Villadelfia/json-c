#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include "json.h"

void print_json(json_t *j, int level) {
    for(int i = 0; i < level; i++) {
        printf("  ");
    }

    switch(json_get_type(j)) {
        case JSON_OBJECT:
            printf("OBJECT:\n");
            for(int i = 0; i < json_children_count(j); i++) {
                json_t *c = json_get_child(j, i);
                for(int i = 0; i < (level+1); i++) printf("  ");
                printf("%s:\n", json_get_name(c));
                print_json(c, level + 2);
            }
            break;
        case JSON_ARRAY:
            printf("ARRAY:\n");
            for(int i = 0; i < json_children_count(j); i++) {
                json_t *c = json_get_child(j, i);
                for(int i = 0; i < (level+1); i++) {
                    printf("  ");
                }
                printf("%d:\n", i);
                print_json(c, level + 2);
            }
            break;
        case JSON_TRUE:
            printf("TRUE\n");
            break;
        case JSON_FALSE:
            printf("FALSE\n");
            break;
        case JSON_NULL:
            printf("NULL\n");
            break;
        case JSON_NUMBER:
            printf("NUMBER: %f\n", json_value_as_double(j));
            break;
        case JSON_STRING:
            printf("STRING: %s\n", json_value_as_string(j));
            break;
        default:
            printf("INVALID\n");
            break;
    }

}

int main(int argc, char** argv) {
    // To make sure we are using UTF-8.
    setlocale(LC_ALL, "C");
    setlocale(LC_CTYPE, ".UTF-8");

    const char *s = "{\"special\":[],\"index\":\"acid-vial\",\"name\":\"Acid (vial)\",\"equipment_category\":{\"index\":\"adventuring-gear\",\"name\":\"Adventuring Gear\",\"url\":\"/api/equipment-categories/adventuring-gear\"},\"gear_category\":{\"index\":\"standard-gear\",\"name\":\"Standard Gear\",\"url\":\"/api/equipment-categories/standard-gear\"},\"cost\":{\"quantity\":25,\"unit\":\"gp\"},\"weight\":1,\"desc\":[\"As an action, you can splash the contents of this vial onto a creature within 5 feet of you or throw the vial up to 20 feet, shattering it on impact. In either case, make a ranged attack against a creature or object, treating the acid as an improvised weapon.\",\"On a hit, the target takes 2d6 acid damage.\"],\"url\":\"/api/equipment/acid-vial\",\"contents\":[],\"properties\":[]}";
    json_t *j = json_parse(s, strlen(s));
    if(j == NULL) {
        printf("Failed to parse.\n");
        return 1;
    }

    printf("Parsed: %s\n", json_get_data(j));

    // Example of direct traversal.
    printf("Example of direct traversal to \"equipment_category/index\": ");
    json_t *c = json_traverse(j, "equipment_category/index");
    printf("%s\n\n", json_value_as_string(c));

    // Example of iteration.
    printf("Iterating over the entire tree:\n");
    print_json(j, 0);

    json_free(j);
    return 0;
}
