#include "instructions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"
#include "readline.h"
#include "stringop.h"

static inline uint64_t swapbits(uint64_t p, uint64_t m, int k) {
	uint64_t q = ((p>>k)^p)&m;
	return p^q^(q<<k);
}

uint64_t bitreverse(uint64_t n) {
	static const uint64_t m0 = 0x5555555555555555LLU;
	static const uint64_t m1 = 0x0300c0303030c303LLU;
	static const uint64_t m2 = 0x00c0300c03f0003fLLU;
	static const uint64_t m3 = 0x00000ffc00003fffLLU;
	n = ((n>>1)&m0) | (n&m0)<<1;
	n = swapbits(n, m1, 4);
	n = swapbits(n, m2, 8);
	n = swapbits(n, m3, 20);
	n = (n >> 34) | (n << 30);
	return n;
}

void print_binary(uint64_t val) {
	int i;
	for (i = 0; i < 64; ++i) {
		if (val & 0x8000000000000000LLU) {
			putchar('1');
		} else {
			putchar('0');
		}
		val <<= 1;
	}
}

operand_group_t *find_operand_group(instruction_set_t *set, const char *name) {
	int i;
	for (i = 0; i < set->operand_groups->length; ++i) {
		operand_group_t *g = set->operand_groups->items[i];
		if (strcmp(g->name, name) == 0) {
			return g;
		}
	}
	return NULL;
}

instruction_operand_t *find_instruction_operand(instruction_t *inst, char key) {
	int i;
	for (i = 0; i < inst->operands->length; ++i) {
		instruction_operand_t *op = inst->operands->items[i];
		if (op->key == key) {
			return op;
		}
	}
	return NULL;
}

immediate_t *find_instruction_immediate(instruction_t *inst, char key) {
	int i;
	for (i = 0; i < inst->immediate->length; ++i) {
		immediate_t *imm = inst->immediate->items[i];
		if (imm->ref == key) {
			return imm;
		}
	}
	return NULL;
}

operand_group_t *create_operand_group(const char *name) {
	operand_group_t *g = malloc(sizeof(operand_group_t));
	g->name = malloc(strlen(name) + 1);
	strcpy(g->name, name);
	g->operands = create_list();
	return g;
}

operand_t *create_operand(const char *match, uint64_t val, size_t len) {
	operand_t *op = malloc(sizeof(operand_t));
	op->match = malloc(strlen(match) + 1);
	strcpy(op->match, match);
	op->value = val;
	op->width = len;
	return op;
}

void parse_operand_line(const char *line, instruction_set_t *set) {
	list_t *parts = split_string(line, " \t");
	if (parts->length != 4) {
		fprintf(stderr, "Warning: Skipping invalid definition from instruction set: %s\n", line);
		free_string_list(parts);
		return;
	}
	operand_group_t *g = find_operand_group(set, (char *)parts->items[1]);
	if (g == NULL) {
		g = create_operand_group((char *)parts->items[1]);
		list_add(set->operand_groups, g);
	}
	char *end;
	uint64_t val = (uint64_t)strtol((char *)parts->items[3], &end, 2);
	if (*end != '\0') {
		fprintf(stderr, "Warning: Skipping invalid definition from instruction set: %s\n", line);
		free_string_list(parts);
		return;
	}
	operand_t *op = create_operand((char *)parts->items[2], val, strlen((char *)parts->items[3]));
	list_add(g->operands, op);
	free_string_list(parts);
}

void parse_instruction_line(const char *line, instruction_set_t *set) {
	list_t *parts = split_string(line, " \t");
	if (parts->length <= 2) {
		fprintf(stderr, "Warning: Skipping invalid definition from instruction set: %s\n", line);
		free_string_list(parts);
		return;
	}
	/* Initialize an empty instruction */
	instruction_t *inst = malloc(sizeof(instruction_t));
	inst->match = malloc(strlen(parts->items[1]) + 1);
	strcpy(inst->match, parts->items[1]);
	inst->operands = create_list();
	inst->immediate = create_list();
	inst->value = 0;
	/* Parse match */
	int i;
	for (i = 0; i < strlen(inst->match); ++i) {
		if (inst->match[i] == '@') /* Operand */ {
			char key = inst->match[++i];
			i += 2; /* Skip key, < */
			int g_len = strchr(inst->match + i, '>') - (inst->match + i);
			char *g = malloc(g_len + 1);
			int j;
			for (j = 0; inst->match[i] != '>'; ++j) {
				g[j] = inst->match[i++];
			}
			g[j] = '\0';
			instruction_operand_t *op = malloc(sizeof(instruction_operand_t));
			op->key = key;
			op->group = g;
			operand_group_t *group = find_operand_group(set, g);
			op->width = ((operand_t *)group->operands->items[0])->width;
			/* Note: operand shift is not populated yet */
			list_add(inst->operands, op);
		} else if (inst->match[i] == '%') /* Immediate value */ {
			char key = inst->match[++i];
			i += 2; /* Skip key, < */
			int g_len = strchr(inst->match + i, '>') - (inst->match + i);
			char *g = malloc(g_len + 1);
			int j;
			for (j = 0; inst->match[i] != '>'; ++j) {
				g[j] = inst->match[i++];
			}
			g[j] = '\0';
			size_t width = atoi(g);
			free(g);
			immediate_t *imm = malloc(sizeof(immediate_t));
			imm->ref = key;
			imm->width = width;
			/* Note: immediate value shift is not populated yet */
			list_add(inst->immediate, imm);
		}
	}
	/* Parse value */
	char *_value = malloc(strlen(line) + 1);
	strcpy(_value, line);
	_value = strip_whitespace(_value);
	char *value = _value;
	while (*value++ != ' '); while (*value++ != ' ');
	inst->width = 0;
	int shift = 0;
	for (i = 0; i < strlen(value); ++i) {
		if (value[i] == ' ' || value[i] == '\t') {
			continue;
		}
		if (value[i] == '1') {
			inst->value |= 1 << shift;
			++inst->width;
			++shift;
		} else if (value[i] == '0') {
			++inst->width;
			++shift;
		} else if (value[i] == '@') /* Operand */ {
			char key = value[++i];
			instruction_operand_t *op = find_instruction_operand(inst, key);
			if (op == NULL) {
				fprintf(stderr, "Warning: Skipping invalid definition from instruction set (unknown operand group): %s\n", line);
				free(_value);
				free_string_list(parts);
				return;
			}
			op->shift = shift;
			shift += op->width;
			inst->width += op->width;
		} else if (value[i] == '%') /* Immediate value */ {
			char key = value[++i];
			immediate_t *imm = find_instruction_immediate(inst, key);
			if (imm == NULL) {
				fprintf(stderr, "Warning: Skipping invalid definition from instruction set (unknown immediate value): %s\n", line);
				free(_value);
				free_string_list(parts);
				return;
			}
			imm->shift = shift;
			shift += imm->width;
			inst->width += imm->width;
		}
	}
	inst->value = bitreverse(inst->value) >> (64 - inst->width);
	free(_value);
	free_string_list(parts);
}

instruction_set_t *load_instruction_set(FILE *file) {
	instruction_set_t *result = malloc(sizeof(instruction_set_t));
	result->instructions = create_list();
	result->operand_groups = create_list();
	result->arch = NULL;
	while (!feof(file)) {
		char *line = read_line(file);
		line = strip_whitespace(line);
		if (line[0] == '\0' || line[0] == '#') {
			free(line);
			continue;
		}
		if (strstr(line, "ARCH ") == line) {
			result->arch = malloc(strlen(line) - 4);
			strcpy(result->arch, line + 5);
		}
		if (strstr(line, "OPERAND ") == line) {
			parse_operand_line(line, result);
		}
		if (strstr(line, "INS ") == line) {
			parse_instruction_line(line, result);
		}
		free(line);
	}
	return result;
}

void instruction_set_free(instruction_set_t *set) {
	int i, n;
	for (i = 0; i < set->instructions->length; ++i) {
		instruction_t *inst = set->instructions->items[i];
		/* TODO: This leaks a few other things */
		free(inst->match);
		free(inst);
	}
	list_free(set->instructions);

	for (i = 0; i < set->operand_groups->length; ++i) {
		operand_group_t *group = set->operand_groups->items[i];
		for (n = 0; n < group->operands->length; ++n) {
			operand_t *op = group->operands->items[n];
			free(op->match);
			free(op);
		}
		free(group->name);
		free(group);
	}
	list_free(set->operand_groups);

	if (set->arch != NULL) {
		free(set->arch);
	}
	free(set);
}

instruction_t *match_instruction(const char *text) {
	/* TODO */
	return NULL;
}
