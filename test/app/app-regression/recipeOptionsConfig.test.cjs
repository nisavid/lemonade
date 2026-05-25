const {
  readSource,
  normalizeWhitespace,
  assertIncludes,
  assertNotMatches,
  assertMatches,
} = require('./helpers/source.cjs');

const RECIPE_OPTIONS_CONFIG = 'src/app/src/renderer/recipes/recipeOptionsConfig.ts';
const MODEL_OPTIONS_MODAL = 'src/app/src/renderer/ModelOptionsModal.tsx';

function extractArray(source, name) {
  const escapedName = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const pattern = new RegExp(`${escapedName}[^\\[]*\\[([^\\]]+)\\]`);
  const match = source.match(pattern);
  if (!match) {
    throw new Error(`Could not find ${name} array.`);
  }

  return Array.from(match[1].matchAll(/'([^']+)'/g), (item) => item[1]);
}

function visibleLlamacppOptions(source, recipeOptions) {
  const baseOptions = extractArray(source, "'llamacpp':");
  if (recipeOptions == null || !Object.prototype.hasOwnProperty.call(recipeOptions, 'llamacpp_reranking_adapter')) {
    return baseOptions;
  }

  const adapterOptions = extractArray(source, 'LLAMACPP_RERANKING_ADAPTER_OPTIONS');
  const insertionIndex = baseOptions.indexOf('mergeArgs');
  if (insertionIndex === -1) {
    return [...baseOptions, ...adapterOptions];
  }

  return [
    ...baseOptions.slice(0, insertionIndex),
    ...adapterOptions,
    ...baseOptions.slice(insertionIndex),
  ];
}

const tests = [
  {
    name: 'llama.cpp base launch options do not expose reranking adapter controls',
    run() {
      const source = normalizeWhitespace(readSource(RECIPE_OPTIONS_CONFIG));
      const llamaMapMatch = source.match(/'llamacpp': \[([^\]]+)\]/);

      assertMatches(
        source,
        /LLAMACPP_RERANKING_ADAPTER_OPTIONS/,
        'Reranking adapter controls should live in a named conditional option group.',
      );
      assertMatches(
        source,
        /'llamacpp': \[[^\]]+\]/,
        'The regression guard must fail if the llama.cpp option map cannot be found.',
      );
      assertNotMatches(
        llamaMapMatch[1],
        /llamacppReranking(Adapter|TrueTokenId|LogitScale)/,
        'Plain llama.cpp models should not inherit reranking adapter controls from the base option map.',
      );
    },
  },
  {
    name: 'model options modal asks for model-specific visible options',
    run() {
      const source = normalizeWhitespace(readSource(MODEL_OPTIONS_MODAL));

      assertIncludes(
        source,
        'getOptionsForRecipe(recipe, modelInfo?.recipe_options',
        'ModelOptionsModal should pass model recipe options when deriving visible launch options.',
      );
      assertIncludes(
        source,
        'createDefaultOptions(options.recipe, modelInfo?.recipe_options',
        'Resetting model options should preserve model-specific conditional controls.',
      );
    },
  },
  {
    name: 'reranking adapter controls are visible only for adapter-backed llama.cpp models',
    run() {
      const source = normalizeWhitespace(readSource(RECIPE_OPTIONS_CONFIG));
      const normalOptions = visibleLlamacppOptions(source, {});
      const nullOptions = visibleLlamacppOptions(source, null);
      const adapterOptions = visibleLlamacppOptions(source, {
        llamacpp_reranking_adapter: 'zeroentropy-logit-score',
      });

      assertNotMatches(
        normalOptions.join(','),
        /llamacppReranking(Adapter|TrueTokenId|LogitScale)/,
        'Normal llama.cpp model options should hide adapter controls.',
      );
      assertNotMatches(
        nullOptions.join(','),
        /llamacppReranking(Adapter|TrueTokenId|LogitScale)/,
        'Null recipe options should be treated like absent recipe options.',
      );
      assertMatches(
        adapterOptions.join(','),
        /llamacppRerankingAdapter,llamacppRerankingTrueTokenId,llamacppRerankingLogitScale/,
        'Adapter-backed llama.cpp model options should include the complete adapter control group.',
      );
    },
  },
];

module.exports = { tests };
