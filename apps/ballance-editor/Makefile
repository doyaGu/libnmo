.PHONY: check-style format test

check-style:
	python3 -m ruff check .
	python3 -m ruff format --check .
	npm run check

format:
	python3 -m ruff format .
	npm run format

test:
	python3 -m unittest discover -s tests -v
	npm test
