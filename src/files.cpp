#include "files.h"
#include <util/platform.h>
#include "plugin-macros.generated.h"

#define CHUNK_LEN 256

static void remove_starting_new_line(char **text_ptr)
{
	char *text = *text_ptr;
	size_t len = strlen(text);

	if (len >= 2 && text[0] == '\r' && text[1] == '\n') {
		*text_ptr += 2;
	} else if (len >= 1 && text[0] == '\n') {
		(*text_ptr)++;
	}
}

static bool remove_ending_new_line(char *text)
{
	size_t len = strlen(text);

	if (len >= 2 && text[len - 2] == '\r' && text[len - 1] == '\n') {
		text[len - 2] = 0;
		text[len - 1] = 0;
		return true;
	} else if (len >= 1 && text[len - 1] == '\n') {
		text[len - 1] = 0;
		return true;
	}
	return false;
}

static void remove_new_lines(size_t start, vector<char *> &texts)
{
	// Remove trailing new lines
	for (size_t i = start; i < texts.size(); i++) {
		remove_ending_new_line(texts[i]);
	}
}

static void load_text_from_file(vector<char *> &texts, const char *file_path,
				const char *delim)
{
	FILE *file = os_fopen(file_path, "rb"); /* should check the result */
	if (file == NULL) {
		blog(LOG_WARNING, "Failed to open file %s", file_path);
		return;
	}

	uint16_t header = 0;
	size_t num_read = fread(&header, 2, 1, file);
	if (num_read == 1 && (header == 0xFEFF || header == 0xFFFE)) {
		blog(LOG_WARNING, "UTF-16 not supported for file %s",
		     file_path);
		fclose(file);
		return;
	}

	fseek(file, 0, SEEK_SET);

	char chunk[CHUNK_LEN];
	memset(chunk, 0, CHUNK_LEN);
	bool add_new_line = true;
	size_t read = 0;
	bool removed_prev_end_nl = false;
	bool removed_end_nl = false;

	while ((read = fread(chunk, sizeof(char), CHUNK_LEN - 1, file))) {

		bool end_in_delim = chunk[read - 1] == *delim;
		add_new_line = add_new_line || chunk[0] == *delim;
		chunk[read] = 0;

#ifdef _WIN32
		char *next_token = NULL;
		char *token = strtok_s(chunk, delim, &next_token);
#else
		char *token = strtok(chunk, delim);
#endif

		while (token) {

			removed_end_nl = remove_ending_new_line(token);
			size_t token_len = strlen(token);

			if (add_new_line) {
				remove_starting_new_line(&token);

				// Need to add new string
				char *curr_text =
					(char *)bzalloc(token_len + 1);

				if (curr_text == NULL) {
					fclose(file);
					return;
				}
#ifdef _WIN32
				strncpy_s(curr_text, token_len + 1, token,
					  token_len);
#else
				strncpy(curr_text, token, token_len);
#endif

				texts.push_back(curr_text);

			} else {
				// Need to append to existing string
				size_t curr_index = texts.size() - 1;
				size_t existing_len = strlen(texts[curr_index]);

#ifdef _WIN32
				unsigned int new_line_len =
					removed_prev_end_nl ? 2 : 0;
#else
				unsigned int new_line_len =
					removed_prev_end_nl ? 1 : 0;
#endif

				char *new_ptr = (char *)brealloc(
					(void *)texts[curr_index],
					new_line_len + existing_len +
						token_len + 1);

				if (new_ptr == NULL) {
					fclose(file);
					return;
				}

#ifdef _WIN32
				if (removed_prev_end_nl) {
					new_ptr[existing_len] = '\r';
					new_ptr[existing_len + 1] = '\n';
				}

				strncpy_s(new_ptr + existing_len + new_line_len,
					  token_len + 1, token, token_len);
#else
				if (removed_prev_end_nl) {
					new_ptr[existing_len] = '\n';
				}

				strncpy(new_ptr + existing_len + new_line_len,
					token, token_len);
#endif

				new_ptr[existing_len + token_len] = 0;
				texts[curr_index] = new_ptr;

				add_new_line = true;
			}

#ifdef _WIN32
			token = strtok_s(NULL, delim, &next_token);
#else
			token = strtok(NULL, delim);
#endif
		}

		removed_prev_end_nl = removed_end_nl;
		add_new_line = end_in_delim;
	}

	remove_new_lines(texts.size() - 1, texts);

	fclose(file);
}

static void load_text_from_file(vector<char *> &texts, const char *file_path)
{
	FILE *file = os_fopen(file_path, "rb"); /* should check the result */
	if (file == NULL) {
		blog(LOG_WARNING, "Failed to open file %s", file_path);
		return;
	}

	uint16_t header = 0;
	size_t num_read = fread(&header, 2, 1, file);
	if (num_read == 1 && (header == 0xFEFF || header == 0xFFFE)) {
		blog(LOG_WARNING, "UTF-16 not supported for file %s",
		     file_path);
		fclose(file);
		return;
	}

	fseek(file, 0, SEEK_SET);

	char line[CHUNK_LEN];
	memset(line, 0, CHUNK_LEN);
	bool add_new_line = true;
	bool prev_new_line = false;

	while (fgets(line, sizeof(line), file)) {
		size_t curr_len = strlen(line);

		if ((curr_len == 2 && line[curr_len - 2] == '\r' &&
		     line[curr_len - 1] == '\n') ||
		    (curr_len == 1 && line[curr_len - 1] == '\n')) {

			add_new_line = true;

			if (!prev_new_line) {
				prev_new_line = true;
				continue;
			}
		}

		if (add_new_line) {
			// Need to add new string
			char *curr_text = (char *)bzalloc(curr_len + 1);

			if (curr_text == NULL) {
				fclose(file);
				return;
			}
#ifdef _WIN32
			strncpy_s(curr_text, curr_len + 1, line, curr_len);
#else
			strncpy(curr_text, line, curr_len);
#endif

			texts.push_back(curr_text);
			add_new_line = false;
			prev_new_line = false;

		} else {
			// Need to append to existing string
			size_t curr_index = texts.size() - 1;
			size_t existing_len = strlen(texts[curr_index]);
			char *new_ptr =
				(char *)brealloc((void *)texts[curr_index],
						 existing_len + curr_len + 1);

			if (new_ptr == NULL) {
				fclose(file);
				return;
			}

#ifdef _WIN32
			strncpy_s(new_ptr + existing_len, curr_len + 1, line,
				  curr_len);
#else
			strncpy(new_ptr + existing_len, line, curr_len);
#endif

			new_ptr[existing_len + curr_len] = 0;
			texts[curr_index] = new_ptr;
		}
	}

	remove_new_lines(0, texts);

	fclose(file);
}

void read_file(struct text_slideshow *text_ss, obs_data_t *settings,
	       vector<char *> &texts)
{

	const char *file_path = text_ss->file.c_str();

	if (!file_path || !*file_path || !os_file_exists(file_path)) {
		blog(LOG_WARNING,
		     "Failed to open %s for "
		     "reading",
		     file_path);
	} else {
		if (!text_ss->file.empty()) {

			if (text_ss->custom_delim) {
				load_text_from_file(texts, file_path,
						    text_ss->custom_delim);
			} else {
				load_text_from_file(texts, file_path);
			}
		}
	}
}