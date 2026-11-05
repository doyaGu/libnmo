export default [
    {
        files: ['ballance_editor/*.js', 'tests/*.cjs'],
        rules: {
            curly: ['error', 'all'],
            'one-var': ['error', 'never'],
            'no-sequences': 'error',
        },
    },
];
